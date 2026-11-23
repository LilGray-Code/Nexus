#include "nxfs.h"

#include <stddef.h>
#include <stdint.h>

#define NXFS_BLOCK_MAX 4096U
#define NXFS_SUPERBLOCK_A 0U
#define NXFS_SUPERBLOCK_B 1U
#define NXFS_JOURNAL_LBA 2U
#define NXFS_METADATA_LBA 3U
#define NXFS_DATA_LBA 67U
#define NXFS_ENTRY_BYTES 64U
#define NXFS_JOURNAL_PAYLOAD 32U
#define NXFS_JOURNAL_UPDATE 1U
#define NXFS_JOURNAL_DELETE 2U

static const uint8_t nxfs_type_guid[16] = {
    0x46U, 0x53U, 0x58U, 0x4EU, 0x01U, 0x00U, 0x58U, 0x4EU,
    0x9AU, 0x11U, 0x4EU, 0x58U, 0x55U, 0x53U, 0x46U, 0x53U
};

static const char super_magic[8] = {'N','E','X','U','S','F','S','1'};
static const char entry_magic[8] = {'N','X','E','N','T','R','Y','1'};
static const char journal_magic[8] = {'N','X','J','O','U','R','N','1'};

static const struct nexus_block_device *nxfs_device;
static struct nexus_gpt_partition nxfs_partition;
static uint32_t nxfs_block_size;
static uint32_t nxfs_blocks_per_slot;
static bool nxfs_mounted;
static bool nxfs_busy;
static bool nxfs_install_authorized;
static const struct nexus_block_device *nxfs_install_device;
static struct nexus_nxfs_entry nxfs_entries[NEXUS_NXFS_MAX_FILES];
static uint8_t nxfs_io_buffer[NXFS_BLOCK_MAX];

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p) {
    return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4U) << 32);
}

static void write_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void write_le64(uint8_t *p, uint64_t value) {
    write_le32(p, (uint32_t)value);
    write_le32(p + 4U, (uint32_t)(value >> 32));
}

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0U; i < length; ++i) out[i] = in[i];
}

static void zero_bytes(void *destination, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    for (size_t i = 0U; i < length; ++i) out[i] = 0U;
}

static bool equal_bytes(const uint8_t *a, const uint8_t *b, size_t length) {
    for (size_t i = 0U; i < length; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data,
                             size_t length) {
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return crc;
}

static uint32_t crc32_bytes(const uint8_t *data, size_t length) {
    return ~crc32_update(0xFFFFFFFFU, data, length);
}

static char ascii_upper(char value) {
    if (value >= 'a' && value <= 'z')
        return (char)(value - 'a' + 'A');
    return value;
}

static bool normalize_name(const char *source,
                           char output[NEXUS_NXFS_NAME_MAX]) {
    if (source == NULL || output == NULL) return false;
    size_t length = 0U;
    bool after_dot = false;
    while (source[length] != '\0') {
        if (length >= NEXUS_NXFS_NAME_MAX - 1U) return false;
        const char ch = source[length];
        if (ch == '.') {
            if (after_dot || length == 0U) return false;
            after_dot = true;
            output[length++] = '.';
            continue;
        }
        const bool valid = (ch >= 'a' && ch <= 'z') ||
                           (ch >= 'A' && ch <= 'Z') ||
                           (ch >= '0' && ch <= '9') ||
                           ch == '_' || ch == '-';
        if (!valid) return false;
        output[length++] = ascii_upper(ch);
    }
    if (length == 0U || output[length - 1U] == '.') return false;
    output[length] = '\0';
    return true;
}

static bool names_equal(const char *a, const char *b) {
    if (a == NULL || b == NULL) return false;
    for (size_t i = 0U; i < NEXUS_NXFS_NAME_MAX; ++i) {
        const char ca = ascii_upper(a[i]);
        const char cb = ascii_upper(b[i]);
        if (ca != cb) return false;
        if (ca == '\0') return true;
    }
    return false;
}

bool nxfs_partition_type_matches(const struct nexus_gpt_partition *partition) {
    return partition != NULL &&
           equal_bytes(partition->type_guid, nxfs_type_guid, 16U);
}

static bool nxfs_read_block(uint64_t relative_lba, uint8_t *buffer) {
    if (nxfs_device == NULL || buffer == NULL ||
        relative_lba >= nxfs_partition.last_lba - nxfs_partition.first_lba + 1U)
        return false;
    return blockdev_read(nxfs_device,
                         nxfs_partition.first_lba + relative_lba,
                         1U, buffer);
}

static bool nxfs_write_block(uint64_t relative_lba, const uint8_t *buffer) {
    if (nxfs_device == NULL || buffer == NULL ||
        relative_lba >= nxfs_partition.last_lba - nxfs_partition.first_lba + 1U)
        return false;
    if (nxfs_install_authorized && nxfs_install_device == nxfs_device)
        return blockdev_install_write(nxfs_device,
            nxfs_partition.first_lba + relative_lba, 1U, buffer);
    return blockdev_write(nxfs_device,
                          nxfs_partition.first_lba + relative_lba,
                          1U, buffer);
}

static bool nxfs_flush(void) {
    if (nxfs_device == NULL) return false;
    if (nxfs_install_authorized && nxfs_install_device == nxfs_device)
        return blockdev_install_flush(nxfs_device);
    return blockdev_flush(nxfs_device);
}

static void encode_entry(const struct nexus_nxfs_entry *entry,
                         uint8_t record[NXFS_ENTRY_BYTES]) {
    zero_bytes(record, NXFS_ENTRY_BYTES);
    copy_bytes(record, entry_magic, sizeof(entry_magic));
    record[8] = entry->used ? 1U : 0U;
    record[9] = entry->bank;
    size_t name_length = 0U;
    while (name_length < NEXUS_NXFS_NAME_MAX &&
           entry->name[name_length] != '\0') ++name_length;
    record[10] = (uint8_t)name_length;
    write_le32(record + 12U, entry->size);
    write_le32(record + 16U, entry->data_crc);
    write_le64(record + 20U, entry->generation);
    copy_bytes(record + 28U, entry->name, NEXUS_NXFS_NAME_MAX);
    write_le32(record + 60U, crc32_bytes(record, 60U));
}

static bool decode_entry(const uint8_t record[NXFS_ENTRY_BYTES],
                         struct nexus_nxfs_entry *entry) {
    if (record == NULL || entry == NULL ||
        !equal_bytes(record, (const uint8_t *)entry_magic, 8U) ||
        read_le32(record + 60U) != crc32_bytes(record, 60U) ||
        record[8] > 1U || record[9] > 1U ||
        record[10] >= NEXUS_NXFS_NAME_MAX) return false;

    *entry = (struct nexus_nxfs_entry){0};
    entry->used = record[8] != 0U;
    entry->bank = record[9];
    entry->size = read_le32(record + 12U);
    entry->data_crc = read_le32(record + 16U);
    entry->generation = read_le64(record + 20U);
    if (!entry->used) return true;
    if (entry->size > NEXUS_NXFS_MAX_FILE_BYTES ||
        record[10] == 0U) return false;

    char raw_name[NEXUS_NXFS_NAME_MAX];
    for (size_t i = 0U; i < NEXUS_NXFS_NAME_MAX; ++i)
        raw_name[i] = (char)record[28U + i];
    raw_name[NEXUS_NXFS_NAME_MAX - 1U] = '\0';
    size_t stored_name_length = 0U;
    while (stored_name_length < NEXUS_NXFS_NAME_MAX - 1U &&
           raw_name[stored_name_length] != '\0') ++stored_name_length;
    if (stored_name_length != record[10]) return false;
    char normalized[NEXUS_NXFS_NAME_MAX];
    if (!normalize_name(raw_name, normalized)) return false;
    if (!names_equal(raw_name, normalized)) return false;
    for (size_t i = 0U; i < NEXUS_NXFS_NAME_MAX; ++i)
        entry->name[i] = normalized[i];
    return true;
}

static uint64_t data_lba(size_t slot, uint8_t bank) {
    return (uint64_t)NXFS_DATA_LBA +
           ((uint64_t)slot * 2U + bank) * nxfs_blocks_per_slot;
}

static bool verify_entry_data(size_t slot,
                              const struct nexus_nxfs_entry *entry) {
    if (entry == NULL || !entry->used ||
        entry->size > NEXUS_NXFS_MAX_FILE_BYTES ||
        entry->bank > 1U) return false;
    uint32_t remaining = entry->size;
    uint64_t crc = 0xFFFFFFFFU;
    uint64_t relative_lba = data_lba(slot, entry->bank);
    while (remaining != 0U) {
        if (!nxfs_read_block(relative_lba++, nxfs_io_buffer)) return false;
        const uint32_t amount = remaining < nxfs_block_size
            ? remaining : nxfs_block_size;
        crc = crc32_update((uint32_t)crc, nxfs_io_buffer, amount);
        remaining -= amount;
    }
    return (uint32_t)~crc == entry->data_crc;
}

static void encode_superblock(uint8_t *buffer, uint64_t partition_blocks) {
    zero_bytes(buffer, nxfs_block_size);
    copy_bytes(buffer, super_magic, sizeof(super_magic));
    write_le32(buffer + 8U, 2U);
    write_le32(buffer + 12U, nxfs_block_size);
    write_le32(buffer + 16U, NEXUS_NXFS_MAX_FILES);
    write_le32(buffer + 20U, NEXUS_NXFS_MAX_FILE_BYTES);
    write_le64(buffer + 24U, partition_blocks);
    write_le32(buffer + 32U, NXFS_METADATA_LBA);
    write_le32(buffer + 36U, NXFS_DATA_LBA);
    write_le32(buffer + 40U, nxfs_blocks_per_slot);
    write_le32(buffer + 60U, crc32_bytes(buffer, 60U));
}

static bool validate_superblock(const uint8_t *buffer,
                                uint64_t partition_blocks) {
    return equal_bytes(buffer, (const uint8_t *)super_magic, 8U) &&
        read_le32(buffer + 8U) == 2U &&
        read_le32(buffer + 12U) == nxfs_block_size &&
        read_le32(buffer + 16U) == NEXUS_NXFS_MAX_FILES &&
        read_le32(buffer + 20U) == NEXUS_NXFS_MAX_FILE_BYTES &&
        read_le64(buffer + 24U) == partition_blocks &&
        read_le32(buffer + 32U) == NXFS_METADATA_LBA &&
        read_le32(buffer + 36U) == NXFS_DATA_LBA &&
        read_le32(buffer + 40U) == nxfs_blocks_per_slot &&
        read_le32(buffer + 60U) == crc32_bytes(buffer, 60U);
}

static bool write_entry_to_disk(size_t slot,
                                const struct nexus_nxfs_entry *entry) {
    uint8_t record[NXFS_ENTRY_BYTES];
    encode_entry(entry, record);
    zero_bytes(nxfs_io_buffer, nxfs_block_size);
    copy_bytes(nxfs_io_buffer, record, sizeof(record));
    return nxfs_write_block(NXFS_METADATA_LBA + slot, nxfs_io_buffer);
}

static bool clear_journal(void) {
    zero_bytes(nxfs_io_buffer, nxfs_block_size);
    return nxfs_write_block(NXFS_JOURNAL_LBA, nxfs_io_buffer) &&
           nxfs_flush();
}

static bool commit_entry(size_t slot,
                         const struct nexus_nxfs_entry *entry) {
    if (slot >= NEXUS_NXFS_MAX_FILES || entry == NULL ||
        !nxfs_is_writable()) return false;

    uint8_t record[NXFS_ENTRY_BYTES];
    encode_entry(entry, record);
    zero_bytes(nxfs_io_buffer, nxfs_block_size);
    copy_bytes(nxfs_io_buffer, journal_magic, sizeof(journal_magic));
    nxfs_io_buffer[8] = 1U;
    nxfs_io_buffer[9] = entry->used ? NXFS_JOURNAL_UPDATE
                                     : NXFS_JOURNAL_DELETE;
    write_le32(nxfs_io_buffer + 12U, (uint32_t)slot);
    copy_bytes(nxfs_io_buffer + NXFS_JOURNAL_PAYLOAD, record, sizeof(record));
    write_le32(nxfs_io_buffer + nxfs_block_size - 4U,
               crc32_bytes(nxfs_io_buffer, nxfs_block_size - 4U));

    if (!nxfs_write_block(NXFS_JOURNAL_LBA, nxfs_io_buffer) ||
        !nxfs_flush() || !write_entry_to_disk(slot, entry) ||
        !nxfs_flush() || !clear_journal()) return false;

    nxfs_entries[slot] = *entry;
    return true;
}

static bool replay_journal(bool *override_valid, size_t *override_slot,
                           struct nexus_nxfs_entry *override_entry) {
    *override_valid = false;
    if (!nxfs_read_block(NXFS_JOURNAL_LBA, nxfs_io_buffer)) return false;
    if (!equal_bytes(nxfs_io_buffer, (const uint8_t *)journal_magic, 8U)) {
        bool nonzero = false;
        for (uint32_t i = 0U; i < nxfs_block_size; ++i)
            if (nxfs_io_buffer[i] != 0U) nonzero = true;
        if (nonzero && nxfs_is_writable()) return clear_journal();
        return true;
    }

    const bool journal_valid = nxfs_io_buffer[8] == 1U &&
        read_le32(nxfs_io_buffer + nxfs_block_size - 4U) ==
            crc32_bytes(nxfs_io_buffer, nxfs_block_size - 4U);
    const size_t slot = (size_t)read_le32(nxfs_io_buffer + 12U);
    struct nexus_nxfs_entry entry;
    const bool entry_valid =
        decode_entry(nxfs_io_buffer + NXFS_JOURNAL_PAYLOAD, &entry);
    const uint8_t operation = nxfs_io_buffer[9];
    if (!journal_valid || slot >= NEXUS_NXFS_MAX_FILES || !entry_valid ||
        (entry.used && operation != NXFS_JOURNAL_UPDATE) ||
        (!entry.used && operation != NXFS_JOURNAL_DELETE) ||
        (entry.used && !verify_entry_data(slot, &entry))) {
        if (nxfs_is_writable()) return clear_journal();
        return true;
    }

    if (nxfs_is_writable()) {
        if (!write_entry_to_disk(slot, &entry) || !nxfs_flush() ||
            !clear_journal()) return false;
    } else {
        *override_valid = true;
        *override_slot = slot;
        *override_entry = entry;
    }
    return true;
}

bool nxfs_format(const struct nexus_block_device *device,
                 const struct nexus_gpt_partition *partition) {
    if (device == NULL || partition == NULL ||
        !nxfs_partition_type_matches(partition) ||
        (!(nxfs_install_authorized && nxfs_install_device == device) &&
         ((device->flags & NEXUS_BLOCKDEV_WRITABLE) == 0U ||
          (device->flags & NEXUS_BLOCKDEV_READ_ONLY) != 0U)) ||
        (device->block_size != 512U && device->block_size != 4096U) ||
        partition->first_lba > partition->last_lba ||
        partition->last_lba >= device->block_count) return false;

    nxfs_device = device;
    nxfs_partition = *partition;
    nxfs_block_size = device->block_size;
    nxfs_blocks_per_slot =
        (NEXUS_NXFS_MAX_FILE_BYTES + nxfs_block_size - 1U) /
        nxfs_block_size;
    const uint64_t partition_blocks =
        partition->last_lba - partition->first_lba + 1U;
    const uint64_t required_blocks =
        (uint64_t)NXFS_DATA_LBA +
        (uint64_t)NEXUS_NXFS_MAX_FILES * 2U * nxfs_blocks_per_slot;
    if (nxfs_block_size > NXFS_BLOCK_MAX ||
        partition_blocks < required_blocks) {
        nxfs_device = NULL;
        return false;
    }

    nxfs_mounted = false;
    nxfs_busy = false;
    zero_bytes(nxfs_entries, sizeof(nxfs_entries));
    zero_bytes(nxfs_io_buffer, nxfs_block_size);

    /* Invalidate an existing format before replacing metadata. */
    if (!nxfs_write_block(NXFS_SUPERBLOCK_A, nxfs_io_buffer) ||
        !nxfs_flush() ||
        !nxfs_write_block(NXFS_SUPERBLOCK_B, nxfs_io_buffer) ||
        !nxfs_flush()) {
        nxfs_device = NULL;
        return false;
    }

    /* Clear the one-block journal and fixed metadata table. */
    if (!nxfs_write_block(NXFS_JOURNAL_LBA, nxfs_io_buffer)) {
        nxfs_device = NULL;
        return false;
    }
    for (size_t slot = 0U; slot < NEXUS_NXFS_MAX_FILES; ++slot) {
        if (!nxfs_write_block(NXFS_METADATA_LBA + slot, nxfs_io_buffer)) {
            nxfs_device = NULL;
            return false;
        }
    }
    if (!nxfs_flush()) {
        nxfs_device = NULL;
        return false;
    }

    /* Two independently checksummed copies allow one-copy recovery. */
    encode_superblock(nxfs_io_buffer, partition_blocks);
    if (!nxfs_write_block(NXFS_SUPERBLOCK_A, nxfs_io_buffer) ||
        !nxfs_flush() ||
        !nxfs_write_block(NXFS_SUPERBLOCK_B, nxfs_io_buffer) ||
        !nxfs_flush()) {
        nxfs_device = NULL;
        return false;
    }

    return nxfs_mount(device, partition);
}

bool nxfs_write_file_install(
    const struct nexus_block_device *device,
    const struct nexus_gpt_partition *partition,
    const char *typed_stable_id,
    const char *typed_erase_phrase,
    const char *name,const void *data,uint32_t length) {
    if (device == NULL || partition == NULL || name == NULL ||
        (length != 0U && data == NULL) || nxfs_install_authorized ||
        nxfs_mounted || !nxfs_partition_type_matches(partition) ||
        partition->first_lba > partition->last_lba ||
        partition->last_lba >= device->block_count ||
        length > NEXUS_NXFS_MAX_FILE_BYTES ||
        !blockdev_install_session_begin(device, typed_stable_id,
                                        typed_erase_phrase))
        return false;
    nxfs_install_device = device;
    nxfs_install_authorized = true;
    bool result = nxfs_mount(device, partition);
    if (result) result = nxfs_write_file(name, data, length);
    /* Leave the target unmounted and close the one-operation write session. */
    nxfs_mounted = false;
    nxfs_busy = false;
    nxfs_device = NULL;
    zero_bytes(nxfs_entries, sizeof(nxfs_entries));
    nxfs_install_authorized = false;
    nxfs_install_device = NULL;
    blockdev_install_session_end();
    return result;
}

bool nxfs_format_install(const struct nexus_block_device *device,
                         const struct nexus_gpt_partition *partition,
                         const char *typed_stable_id,
                         const char *typed_erase_phrase) {
    if (device == NULL || partition == NULL || nxfs_install_authorized ||
        nxfs_mounted || !nxfs_partition_type_matches(partition) ||
        partition->first_lba > partition->last_lba ||
        partition->last_lba >= device->block_count ||
        !blockdev_install_session_begin(device, typed_stable_id,
                                        typed_erase_phrase))
        return false;
    nxfs_install_device = device;
    nxfs_install_authorized = true;
    const bool result = nxfs_format(device, partition);
    /* Formatting verifies by mounting, then leaves the volume unmounted so
     * the installer can continue deploying files or formatting another disk. */
    nxfs_mounted = false;
    nxfs_busy = false;
    nxfs_device = NULL;
    zero_bytes(nxfs_entries, sizeof(nxfs_entries));
    nxfs_install_authorized = false;
    nxfs_install_device = NULL;
    blockdev_install_session_end();
    return result;
}

bool nxfs_mount(const struct nexus_block_device *device,
                const struct nexus_gpt_partition *partition) {
    blockdev_nexus_data_write_disable(nxfs_device);
    nxfs_mounted = false;
    nxfs_busy = false;
    nxfs_device = NULL;
    zero_bytes(nxfs_entries, sizeof(nxfs_entries));

    if (device == NULL || partition == NULL ||
        !nxfs_partition_type_matches(partition) ||
        (device->block_size != 512U && device->block_size != 4096U) ||
        partition->first_lba > partition->last_lba ||
        partition->last_lba >= device->block_count) return false;

    nxfs_device = device;
    nxfs_partition = *partition;
    nxfs_block_size = device->block_size;
    nxfs_blocks_per_slot =
        (NEXUS_NXFS_MAX_FILE_BYTES + nxfs_block_size - 1U) /
        nxfs_block_size;
    const uint64_t partition_blocks =
        partition->last_lba - partition->first_lba + 1U;
    const uint64_t required_blocks =
        (uint64_t)NXFS_DATA_LBA +
        (uint64_t)NEXUS_NXFS_MAX_FILES * 2U * nxfs_blocks_per_slot;
    if (partition_blocks < required_blocks ||
        nxfs_block_size > NXFS_BLOCK_MAX) {
        nxfs_device = NULL;
        return false;
    }

    uint8_t super_a[NXFS_BLOCK_MAX];
    uint8_t super_b[NXFS_BLOCK_MAX];
    const bool read_a = nxfs_read_block(NXFS_SUPERBLOCK_A, super_a);
    const bool read_b = nxfs_read_block(NXFS_SUPERBLOCK_B, super_b);
    const bool valid_a = read_a &&
        validate_superblock(super_a, partition_blocks);
    const bool valid_b = read_b &&
        validate_superblock(super_b, partition_blocks);
    if (!valid_a && !valid_b) {
        nxfs_device = NULL;
        return false;
    }
    if (valid_a && valid_b && !equal_bytes(super_a, super_b, 60U)) {
        nxfs_device = NULL;
        return false;
    }

    bool override_valid = false;
    size_t override_slot = 0U;
    struct nexus_nxfs_entry override_entry = {0};
    if (!replay_journal(&override_valid, &override_slot, &override_entry)) {
        nxfs_device = NULL;
        return false;
    }

    for (size_t i = 0U; i < NEXUS_NXFS_MAX_FILES; ++i) {
        if (!nxfs_read_block(NXFS_METADATA_LBA + i, nxfs_io_buffer)) {
            nxfs_device = NULL;
            return false;
        }
        struct nexus_nxfs_entry entry;
        if (decode_entry(nxfs_io_buffer, &entry) && entry.used &&
            verify_entry_data(i, &entry)) {
            bool duplicate = false;
            for (size_t prior = 0U; prior < i; ++prior) {
                if (nxfs_entries[prior].used &&
                    names_equal(nxfs_entries[prior].name, entry.name))
                    duplicate = true;
            }
            if (!duplicate) nxfs_entries[i] = entry;
        }
    }
    if (override_valid) {
        nxfs_entries[override_slot] = override_entry;
    }
    nxfs_mounted = true;
    /* A read-only release driver becomes writable only for the dedicated
     * data partition, and only when this disk is the identified boot medium. */
    (void)blockdev_nexus_data_write_enable(device, partition->first_lba,
                                           partition->last_lba);
    return true;
}

bool nxfs_is_mounted(void) {
    return nxfs_mounted;
}

bool nxfs_is_writable(void) {
    if (nxfs_device == NULL) return false;
    if (nxfs_install_authorized && nxfs_install_device == nxfs_device)
        return true;
    if ((nxfs_device->flags & NEXUS_BLOCKDEV_WRITABLE) != 0U &&
        (nxfs_device->flags & NEXUS_BLOCKDEV_READ_ONLY) == 0U)
        return true;
    return blockdev_nexus_data_write_is_enabled(nxfs_device,
        nxfs_partition.first_lba, nxfs_partition.last_lba);
}

size_t nxfs_entry_count(void) {
    size_t count = 0U;
    if (!nxfs_mounted) return 0U;
    for (size_t i = 0U; i < NEXUS_NXFS_MAX_FILES; ++i)
        if (nxfs_entries[i].used) ++count;
    return count;
}

const struct nexus_nxfs_entry *nxfs_entry_at(size_t index) {
    if (!nxfs_mounted) return NULL;
    for (size_t i = 0U; i < NEXUS_NXFS_MAX_FILES; ++i) {
        if (!nxfs_entries[i].used) continue;
        if (index == 0U) return &nxfs_entries[i];
        --index;
    }
    return NULL;
}

bool nxfs_read_file(const struct nexus_nxfs_entry *entry,
                    uint32_t offset, uint32_t length, void *buffer) {
    if (!nxfs_mounted || nxfs_busy || entry == NULL ||
        (length != 0U && buffer == NULL) || offset > entry->size ||
        length > entry->size - offset) return false;

    size_t slot = NEXUS_NXFS_MAX_FILES;
    if (!entry->used || entry->bank > 1U ||
        entry->size > NEXUS_NXFS_MAX_FILE_BYTES) return false;
    /*
     * File handles may retain a metadata snapshot. Resolve it to the current
     * slot only while that exact generation is still live; never let a stale
     * handle silently read a replacement file after an overwrite.
     */
    for (size_t i = 0U; i < NEXUS_NXFS_MAX_FILES; ++i) {
        const struct nexus_nxfs_entry *current = &nxfs_entries[i];
        if (current->used && current->bank == entry->bank &&
            current->size == entry->size &&
            current->data_crc == entry->data_crc &&
            current->generation == entry->generation &&
            names_equal(current->name, entry->name)) {
            slot = i;
            break;
        }
    }
    if (slot >= NEXUS_NXFS_MAX_FILES) return false;
    if (length == 0U) return true;

    nxfs_busy = true;
    uint8_t *output = (uint8_t *)buffer;
    uint32_t copied = 0U;
    while (copied < length) {
        const uint32_t absolute = offset + copied;
        const uint32_t block_index = absolute / nxfs_block_size;
        const uint32_t block_offset = absolute % nxfs_block_size;
        if (!nxfs_read_block(data_lba(slot, entry->bank) + block_index,
                             nxfs_io_buffer)) {
            nxfs_busy = false;
            return false;
        }
        uint32_t amount = nxfs_block_size - block_offset;
        if (amount > length - copied) amount = length - copied;
        copy_bytes(output + copied, nxfs_io_buffer + block_offset, amount);
        copied += amount;
    }
    nxfs_busy = false;
    return true;
}

bool nxfs_write_file(const char *name, const void *data, size_t length) {
    char normalized[NEXUS_NXFS_NAME_MAX];
    if (!nxfs_mounted || nxfs_busy || !nxfs_is_writable() ||
        length > NEXUS_NXFS_MAX_FILE_BYTES ||
        (length != 0U && data == NULL) ||
        !normalize_name(name, normalized)) return false;

    size_t slot = NEXUS_NXFS_MAX_FILES;
    for (size_t i = 0U; i < NEXUS_NXFS_MAX_FILES; ++i) {
        if (nxfs_entries[i].used &&
            names_equal(nxfs_entries[i].name, normalized)) {
            slot = i;
            break;
        }
        if (!nxfs_entries[i].used && slot == NEXUS_NXFS_MAX_FILES)
            slot = i;
    }
    if (slot >= NEXUS_NXFS_MAX_FILES) return false;

    nxfs_busy = true;
    const uint8_t bank = nxfs_entries[slot].used
        ? (uint8_t)(nxfs_entries[slot].bank ^ 1U) : 0U;
    const uint8_t *input = (const uint8_t *)data;
    const uint32_t blocks = (uint32_t)(
        (length + nxfs_block_size - 1U) / nxfs_block_size);
    size_t consumed = 0U;
    for (uint32_t block = 0U; block < blocks; ++block) {
        zero_bytes(nxfs_io_buffer, nxfs_block_size);
        size_t amount = length - consumed;
        if (amount > nxfs_block_size) amount = nxfs_block_size;
        if (amount != 0U) copy_bytes(nxfs_io_buffer, input + consumed, amount);
        if (!nxfs_write_block(data_lba(slot, bank) + block, nxfs_io_buffer)) {
            nxfs_busy = false;
            return false;
        }
        consumed += amount;
    }
    if (!nxfs_flush()) {
        nxfs_busy = false;
        return false;
    }

    struct nexus_nxfs_entry entry = {0};
    entry.used = true;
    entry.bank = bank;
    entry.size = (uint32_t)length;
    entry.data_crc = crc32_bytes((const uint8_t *)data, length);
    entry.generation = nxfs_entries[slot].generation + 1U;
    for (size_t i = 0U; i < NEXUS_NXFS_NAME_MAX; ++i)
        entry.name[i] = normalized[i];

    nxfs_busy = false;
    if (!commit_entry(slot, &entry)) return false;
    return true;
}

bool nxfs_remove_file(const char *name) {
    char normalized[NEXUS_NXFS_NAME_MAX];
    if (!nxfs_mounted || nxfs_busy || !nxfs_is_writable() ||
        !normalize_name(name, normalized)) return false;
    size_t slot = NEXUS_NXFS_MAX_FILES;
    for (size_t i = 0U; i < NEXUS_NXFS_MAX_FILES; ++i) {
        if (nxfs_entries[i].used &&
            names_equal(nxfs_entries[i].name, normalized)) {
            slot = i;
            break;
        }
    }
    if (slot >= NEXUS_NXFS_MAX_FILES) return false;

    struct nexus_nxfs_entry tombstone = {0};
    tombstone.generation = nxfs_entries[slot].generation + 1U;
    return commit_entry(slot, &tombstone);
}

static bool stage_journal_only(const char *name, const void *data,
                               size_t length) {
    char normalized[NEXUS_NXFS_NAME_MAX];
    if (!nxfs_is_writable() || !normalize_name(name, normalized) ||
        length > NEXUS_NXFS_MAX_FILE_BYTES || (length != 0U && data == NULL))
        return false;

    size_t slot = NEXUS_NXFS_MAX_FILES;
    for (size_t i = 0U; i < NEXUS_NXFS_MAX_FILES; ++i)
        if (!nxfs_entries[i].used) { slot = i; break; }
    if (slot >= NEXUS_NXFS_MAX_FILES) return false;

    const uint8_t bank = 0U;
    const uint8_t *input = (const uint8_t *)data;
    const uint32_t blocks = (uint32_t)(
        (length + nxfs_block_size - 1U) / nxfs_block_size);
    size_t consumed = 0U;
    for (uint32_t block = 0U; block < blocks; ++block) {
        zero_bytes(nxfs_io_buffer, nxfs_block_size);
        size_t amount = length - consumed;
        if (amount > nxfs_block_size) amount = nxfs_block_size;
        if (amount != 0U) copy_bytes(nxfs_io_buffer, input + consumed, amount);
        if (!nxfs_write_block(data_lba(slot, bank) + block, nxfs_io_buffer))
            return false;
        consumed += amount;
    }
    if (!nxfs_flush()) return false;

    struct nexus_nxfs_entry entry = {0};
    entry.used = true;
    entry.bank = bank;
    entry.size = (uint32_t)length;
    entry.data_crc = crc32_bytes(input, length);
    entry.generation = 1U;
    for (size_t i = 0U; i < NEXUS_NXFS_NAME_MAX; ++i)
        entry.name[i] = normalized[i];

    uint8_t record[NXFS_ENTRY_BYTES];
    encode_entry(&entry, record);
    zero_bytes(nxfs_io_buffer, nxfs_block_size);
    copy_bytes(nxfs_io_buffer, journal_magic, sizeof(journal_magic));
    nxfs_io_buffer[8] = 1U;
    nxfs_io_buffer[9] = NXFS_JOURNAL_UPDATE;
    write_le32(nxfs_io_buffer + 12U, (uint32_t)slot);
    copy_bytes(nxfs_io_buffer + NXFS_JOURNAL_PAYLOAD, record, sizeof(record));
    write_le32(nxfs_io_buffer + nxfs_block_size - 4U,
               crc32_bytes(nxfs_io_buffer, nxfs_block_size - 4U));
    return nxfs_write_block(NXFS_JOURNAL_LBA, nxfs_io_buffer) &&
           nxfs_flush();
}

bool nxfs_selftest(void) {
    if (!nxfs_mounted || !nxfs_is_writable()) return false;
    static const char payload_a[] = "NEXUS NXFS BANK A";
    static const char payload_b[] = "NEXUS NXFS BANK B UPDATED";
    static const char recovery_payload[] = "NEXUS NXFS JOURNAL RECOVERED";
    uint8_t readback[64];

    if (!nxfs_write_file("NEXUS.TST", payload_a, sizeof(payload_a) - 1U))
        return false;
    const struct nexus_nxfs_entry *entry = NULL;
    for (size_t i = 0U; i < NEXUS_NXFS_MAX_FILES; ++i)
        if (nxfs_entries[i].used && names_equal(nxfs_entries[i].name,
                                                 "NEXUS.TST"))
            entry = &nxfs_entries[i];
    if (entry == NULL || entry->size != sizeof(payload_a) - 1U ||
        !nxfs_read_file(entry, 0U, entry->size, readback)) return false;
    for (size_t i = 0U; i < sizeof(payload_a) - 1U; ++i)
        if (readback[i] != (uint8_t)payload_a[i]) return false;

    if (!nxfs_write_file("NEXUS.TST", payload_b, sizeof(payload_b) - 1U))
        return false;
    entry = NULL;
    for (size_t i = 0U; i < NEXUS_NXFS_MAX_FILES; ++i)
        if (nxfs_entries[i].used && names_equal(nxfs_entries[i].name,
                                                 "NEXUS.TST"))
            entry = &nxfs_entries[i];
    if (entry == NULL || entry->size != sizeof(payload_b) - 1U ||
        !nxfs_read_file(entry, 0U, entry->size, readback)) return false;
    for (size_t i = 0U; i < sizeof(payload_b) - 1U; ++i)
        if (readback[i] != (uint8_t)payload_b[i]) return false;
    if (!nxfs_remove_file("NEXUS.TST")) return false;

    const struct nexus_block_device *device = nxfs_device;
    const struct nexus_gpt_partition partition = nxfs_partition;
    if (!stage_journal_only("RECOVER.TST", recovery_payload,
                            sizeof(recovery_payload) - 1U)) return false;

    if (!nxfs_mount(device, &partition)) return false;
    entry = NULL;
    for (size_t i = 0U; i < NEXUS_NXFS_MAX_FILES; ++i)
        if (nxfs_entries[i].used && names_equal(nxfs_entries[i].name,
                                                 "RECOVER.TST"))
            entry = &nxfs_entries[i];
    if (entry == NULL || entry->size != sizeof(recovery_payload) - 1U ||
        !nxfs_read_file(entry, 0U, entry->size, readback)) return false;
    for (size_t i = 0U; i < sizeof(recovery_payload) - 1U; ++i)
        if (readback[i] != (uint8_t)recovery_payload[i]) return false;
    if (!nxfs_remove_file("RECOVER.TST")) return false;

    return true;
}


void nxfs_unmount(void) {
    /* Installer write sessions own their mount lifecycle and cannot be
     * interrupted by the ordinary desktop unmount path. */
    if (nxfs_install_authorized) return;
    blockdev_nexus_data_write_disable(nxfs_device);
    nxfs_mounted = false;
    nxfs_busy = false;
    nxfs_device = NULL;
    zero_bytes(nxfs_entries, sizeof(nxfs_entries));
}
