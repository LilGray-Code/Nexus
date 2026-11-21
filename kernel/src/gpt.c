#include "gpt.h"

#include <stddef.h>
#include <stdint.h>

#define GPT_SIGNATURE "EFI PART"
#define GPT_HEADER_MIN_SIZE 92U
#define GPT_ENTRY_MIN_SIZE 128U
#define GPT_MAX_ENTRY_COUNT 128U
#define GPT_MAX_ENTRY_BYTES (64U * 1024U)
#define GPT_IO_BUFFER_SIZE 4096U

static struct nexus_gpt_partition partitions[NEXUS_GPT_MAX_PARTITIONS];
static size_t partition_count;
static uint8_t io_buffer[GPT_IO_BUFFER_SIZE];
static uint8_t backup_header[GPT_IO_BUFFER_SIZE];

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p) {
    return (uint64_t)read_le32(p) |
           ((uint64_t)read_le32(p + 4) << 32);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data,
                             size_t length) {
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return crc;
}

static bool read_one(const struct nexus_block_device *device,
                     uint64_t lba, uint8_t *buffer) {
    return blockdev_read(device, lba, 1U, buffer);
}

static bool guid_is_zero(const uint8_t *guid) {
    uint8_t value = 0;
    for (size_t i = 0; i < 16U; ++i) value |= guid[i];
    return value == 0;
}

static bool validate_header(const struct nexus_block_device *device,
                            uint8_t *header, uint64_t *entries_lba,
                            uint32_t *entry_count, uint32_t *entry_size,
                            uint32_t *entries_crc, uint64_t *first_usable_out,
                            uint64_t *last_usable_out, uint64_t *backup_lba_out,
                            uint8_t disk_guid_out[16]) {
    if (device == NULL || device->block_size < GPT_HEADER_MIN_SIZE ||
        device->block_size > GPT_IO_BUFFER_SIZE ||
        device->block_size % GPT_ENTRY_MIN_SIZE != 0 ||
        device->block_count < 3U ||
        !read_one(device, 1U, header)) {
        return false;
    }

    static const uint8_t signature[8] = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'};
    for (size_t i = 0; i < sizeof(signature); ++i) {
        if (header[i] != signature[i]) return false;
    }

    const uint32_t header_size = read_le32(header + 12U);
    if (header_size < GPT_HEADER_MIN_SIZE ||
        header_size > device->block_size) return false;

    const uint32_t stored_header_crc = read_le32(header + 16U);
    const uint32_t saved_crc = stored_header_crc;
    header[16] = header[17] = header[18] = header[19] = 0;
    const uint32_t calculated_header_crc =
        ~crc32_update(0xFFFFFFFFU, header, header_size);
    header[16] = (uint8_t)saved_crc;
    header[17] = (uint8_t)(saved_crc >> 8);
    header[18] = (uint8_t)(saved_crc >> 16);
    header[19] = (uint8_t)(saved_crc >> 24);
    if (calculated_header_crc != stored_header_crc) return false;

    const uint64_t current_lba = read_le64(header + 24U);
    const uint64_t backup_lba = read_le64(header + 32U);
    const uint64_t first_usable = read_le64(header + 40U);
    const uint64_t last_usable = read_le64(header + 48U);
    *entries_lba = read_le64(header + 72U);
    *entry_count = read_le32(header + 80U);
    *entry_size = read_le32(header + 84U);
    *entries_crc = read_le32(header + 88U);

    if (current_lba != 1U || backup_lba >= device->block_count ||
        backup_lba == current_lba || first_usable > last_usable ||
        last_usable >= device->block_count ||
        *entries_lba < 2U || *entries_lba >= device->block_count ||
        *entry_count == 0U || *entry_count > GPT_MAX_ENTRY_COUNT ||
        *entry_size < GPT_ENTRY_MIN_SIZE || *entry_size > 512U ||
        (*entry_size & 7U) != 0U || device->block_size % *entry_size != 0U) {
        return false;
    }

    const uint64_t total_bytes = (uint64_t)*entry_count * *entry_size;
    if (total_bytes > GPT_MAX_ENTRY_BYTES) return false;
    const uint64_t entry_blocks =
        (total_bytes + device->block_size - 1U) / device->block_size;
    if (entry_blocks == 0U || *entries_lba >= device->block_count ||
        entry_blocks > device->block_count - *entries_lba ||
        *entries_lba + entry_blocks > first_usable) {
        return false;
    }
    *first_usable_out = first_usable;
    *last_usable_out = last_usable;
    *backup_lba_out = backup_lba;
    for (size_t i = 0; i < 16U; ++i) disk_guid_out[i] = header[56U + i];
    return true;
}


static bool validate_backup_header(const struct nexus_block_device *device,
                                   uint64_t backup_lba,
                                   uint64_t first_usable,
                                   uint64_t last_usable,
                                   uint64_t primary_entries_lba,
                                   uint32_t entry_count,
                                   uint32_t entry_size,
                                   uint32_t entries_crc,
                                   const uint8_t disk_guid[16]) {
    if (!read_one(device, backup_lba, backup_header)) return false;
    static const uint8_t signature[8] =
        {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'};
    for (size_t i = 0; i < 8U; ++i) {
        if (backup_header[i] != signature[i]) return false;
    }
    const uint32_t header_size = read_le32(backup_header + 12U);
    if (header_size < GPT_HEADER_MIN_SIZE ||
        header_size > device->block_size) return false;
    const uint32_t stored_crc = read_le32(backup_header + 16U);
    backup_header[16] = backup_header[17] =
        backup_header[18] = backup_header[19] = 0;
    const uint32_t calculated_crc =
        ~crc32_update(0xFFFFFFFFU, backup_header, header_size);
    backup_header[16] = (uint8_t)stored_crc;
    backup_header[17] = (uint8_t)(stored_crc >> 8);
    backup_header[18] = (uint8_t)(stored_crc >> 16);
    backup_header[19] = (uint8_t)(stored_crc >> 24);
    if (calculated_crc != stored_crc ||
        read_le64(backup_header + 24U) != backup_lba ||
        read_le64(backup_header + 32U) != 1U ||
        read_le64(backup_header + 40U) != first_usable ||
        read_le64(backup_header + 48U) != last_usable ||
        read_le32(backup_header + 80U) != entry_count ||
        read_le32(backup_header + 84U) != entry_size ||
        read_le32(backup_header + 88U) != entries_crc) {
        return false;
    }
    for (size_t i = 0; i < 16U; ++i) {
        if (backup_header[56U + i] != disk_guid[i]) return false;
    }

    const uint64_t backup_entries_lba = read_le64(backup_header + 72U);
    const uint64_t entry_bytes = (uint64_t)entry_count * entry_size;
    const uint64_t entry_blocks =
        (entry_bytes + device->block_size - 1U) / device->block_size;
    if (backup_entries_lba <= primary_entries_lba ||
        backup_entries_lba >= backup_lba ||
        entry_blocks > backup_lba - backup_entries_lba ||
        backup_entries_lba + entry_blocks != backup_lba) {
        return false;
    }

    uint64_t remaining = entry_bytes;
    uint64_t lba = backup_entries_lba;
    uint32_t crc = 0xFFFFFFFFU;
    while (remaining > 0U) {
        if (!read_one(device, lba++, io_buffer)) return false;
        const size_t bytes = remaining < device->block_size
            ? (size_t)remaining : device->block_size;
        crc = crc32_update(crc, io_buffer, bytes);
        remaining -= bytes;
    }
    return ~crc == entries_crc;
}

bool gpt_scan_device(const struct nexus_block_device *device) {
    partition_count = 0;
    for (size_t i = 0; i < NEXUS_GPT_MAX_PARTITIONS; ++i)
        partitions[i] = (struct nexus_gpt_partition){0};

    if (device == NULL || device->block_size > sizeof(io_buffer) ||
        device->block_size < GPT_ENTRY_MIN_SIZE ||
        device->block_size % GPT_ENTRY_MIN_SIZE != 0 ||
        device->block_count < 3U || !read_one(device, 0U, io_buffer) ||
        io_buffer[510] != 0x55U || io_buffer[511] != 0xAAU ||
        io_buffer[450] != 0xEEU || read_le32(io_buffer + 454U) != 1U) {
        return false;
    }

    uint64_t entries_lba = 0;
    uint32_t entry_count = 0;
    uint32_t entry_size = 0;
    uint32_t expected_entries_crc = 0;
    uint64_t first_usable = 0;
    uint64_t last_usable = 0;
    uint64_t backup_lba = 0;
    uint8_t disk_guid[16];
    if (!validate_header(device, io_buffer, &entries_lba, &entry_count,
                         &entry_size, &expected_entries_crc,
                         &first_usable, &last_usable, &backup_lba,
                         disk_guid)) {
        return false;
    }

    if (!validate_backup_header(device, backup_lba, first_usable,
                                last_usable, entries_lba, entry_count,
                                entry_size, expected_entries_crc, disk_guid)) {
        return false;
    }

    const uint64_t total_bytes = (uint64_t)entry_count * entry_size;
    uint64_t remaining = total_bytes;
    uint64_t lba = entries_lba;
    uint32_t crc = 0xFFFFFFFFU;
    while (remaining > 0U) {
        if (!read_one(device, lba++, io_buffer)) return false;
        const size_t bytes = remaining < device->block_size
            ? (size_t)remaining : device->block_size;
        crc = crc32_update(crc, io_buffer, bytes);
        remaining -= bytes;
    }
    if (~crc != expected_entries_crc) return false;

    const uint64_t entries_per_block = device->block_size / entry_size;
    uint32_t entry_index = 0;
    lba = entries_lba;
    while (entry_index < entry_count) {
        if (!read_one(device, lba++, io_buffer)) {
            partition_count = 0;
            return false;
        }
        for (uint64_t slot = 0; slot < entries_per_block &&
             entry_index < entry_count; ++slot, ++entry_index) {
            const uint8_t *entry = io_buffer + slot * entry_size;
            if (guid_is_zero(entry)) continue;
            const uint64_t first = read_le64(entry + 32U);
            const uint64_t last = read_le64(entry + 40U);
            const uint64_t attributes = read_le64(entry + 48U);
            if (first < first_usable || last > last_usable ||
                first > last || last >= device->block_count ||
                guid_is_zero(entry + 16U) ||
                partition_count >= NEXUS_GPT_MAX_PARTITIONS) {
                partition_count = 0;
                return false;
            }
            for (size_t prior = 0; prior < partition_count; ++prior) {
                const struct nexus_gpt_partition *existing = &partitions[prior];
                if (!(last < existing->first_lba ||
                      first > existing->last_lba)) {
                    partition_count = 0;
                    return false;
                }
                bool same_guid = true;
                for (size_t g = 0; g < 16U; ++g) {
                    if (existing->unique_guid[g] != entry[16U + g])
                        same_guid = false;
                }
                if (same_guid) {
                    partition_count = 0;
                    return false;
                }
            }
            struct nexus_gpt_partition *part =
                &partitions[partition_count++];
            for (size_t i = 0; i < 16U; ++i) {
                part->type_guid[i] = entry[i];
                part->unique_guid[i] = entry[16U + i];
            }
            part->first_lba = first;
            part->last_lba = last;
            part->attributes = attributes;
        }
    }
    return true;
}

size_t gpt_partition_count(void) {
    return partition_count;
}

const struct nexus_gpt_partition *gpt_partition_at(size_t index) {
    if (index >= partition_count) return NULL;
    return &partitions[index];
}
