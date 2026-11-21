#include "gpt_image.h"

#include <stddef.h>
#include <stdint.h>

#define GPT_HEADER_SIZE 92U
#define GPT_PRIMARY_ENTRIES_LBA 2U
#define GPT_PROTECTIVE_TYPE 0xEEU

static const uint8_t esp_type_guid[16] = {
    0x28U, 0x73U, 0x2AU, 0xC1U, 0x1FU, 0xF8U, 0xD2U, 0x11U,
    0xBAU, 0x4BU, 0x00U, 0xA0U, 0xC9U, 0x3EU, 0xC9U, 0x3BU
};
static const uint8_t bios_boot_type_guid[16] = {
    0x48U, 0x61U, 0x68U, 0x21U, 0x49U, 0x64U, 0x6FU, 0x6EU,
    0x74U, 0x4EU, 0x65U, 0x65U, 0x64U, 0x45U, 0x46U, 0x49U
};
static const uint8_t nxfs_type_guid[16] = {
    0x46U, 0x53U, 0x58U, 0x4EU, 0x01U, 0x00U, 0x58U, 0x4EU,
    0x9AU, 0x11U, 0x4EU, 0x58U, 0x55U, 0x53U, 0x46U, 0x53U
};

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    for (size_t i = 0U; i < length; ++i) bytes[i] = 0U;
}

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0U; i < length; ++i) out[i] = in[i];
}

static void write_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
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

static uint32_t crc32_bytes(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

static bool guid_nonzero(const uint8_t guid[16]) {
    uint8_t value = 0U;
    for (size_t i = 0U; i < 16U; ++i) value |= guid[i];
    return value != 0U;
}

static bool guid_equal(const uint8_t a[16], const uint8_t b[16]) {
    for (size_t i = 0U; i < 16U; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

static const uint8_t *partition_type(enum nexus_partition_kind kind) {
    switch (kind) {
        case NEXUS_PARTITION_ESP: return esp_type_guid;
        case NEXUS_PARTITION_BIOS_BOOT: return bios_boot_type_guid;
        case NEXUS_PARTITION_SYSTEM:
        case NEXUS_PARTITION_DATA: return nxfs_type_guid;
        default: return NULL;
    }
}

static const char *partition_name(enum nexus_partition_kind kind) {
    switch (kind) {
        case NEXUS_PARTITION_ESP: return "Nexus EFI";
        case NEXUS_PARTITION_BIOS_BOOT: return "Nexus BIOS";
        case NEXUS_PARTITION_SYSTEM: return "Nexus System";
        case NEXUS_PARTITION_DATA: return "Nexus Data";
        default: return NULL;
    }
}

static bool validate_plan(const struct nexus_partition_plan *plan,
                          const uint8_t disk_guid[16],
                          const uint8_t partition_guids[NEXUS_PLAN_MAX_PARTITIONS]) {
    if (plan == NULL || disk_guid == NULL || partition_guids == NULL ||
        !guid_nonzero(disk_guid) ||
        (plan->logical_block_size != 512U &&
         plan->logical_block_size != 4096U) ||
        plan->partition_count == 0U ||
        plan->partition_count > NEXUS_PLAN_MAX_PARTITIONS ||
        plan->disk_block_count < 3U ||
        plan->first_usable_lba > plan->last_usable_lba ||
        plan->last_usable_lba >= plan->disk_block_count - 1U) return false;

    uint64_t previous_last = plan->first_usable_lba - 1U;
    for (size_t i = 0U; i < plan->partition_count; ++i) {
        const struct nexus_planned_partition *part = &plan->partitions[i];
        if (partition_type(part->kind) == NULL ||
            partition_name(part->kind) == NULL ||
            !guid_nonzero(partition_guids[i]) ||
            part->first_lba > part->last_lba ||
            part->first_lba < plan->first_usable_lba ||
            part->last_lba > plan->last_usable_lba ||
            (i != 0U && part->first_lba <= previous_last) ||
            (part->first_lba * (uint64_t)plan->logical_block_size) %
                NEXUS_PLAN_ALIGNMENT_BYTES != 0U) return false;
        if (guid_equal(disk_guid, partition_guids[i])) return false;
        for (size_t prior = 0U; prior < i; ++prior)
            if (guid_equal(partition_guids[prior], partition_guids[i]))
                return false;
        previous_last = part->last_lba;
    }
    return true;
}

static void encode_partition_entry(uint8_t *entry,
                                   const struct nexus_planned_partition *part,
                                   const uint8_t unique_guid[16]) {
    const uint8_t *type_guid = partition_type(part->kind);
    const char *name = partition_name(part->kind);
    zero_bytes(entry, NEXUS_GPT_IMAGE_ENTRY_SIZE);
    copy_bytes(entry, type_guid, 16U);
    copy_bytes(entry + 16U, unique_guid, 16U);
    write_le64(entry + 32U, part->first_lba);
    write_le64(entry + 40U, part->last_lba);
    write_le64(entry + 48U, 0U);
    for (size_t i = 0U; name[i] != '\0' && i < 36U; ++i)
        write_le16(entry + 56U + i * 2U, (uint16_t)(uint8_t)name[i]);
}

static void encode_header(uint8_t *header, uint32_t block_size,
                          uint64_t current_lba, uint64_t backup_lba,
                          uint64_t first_usable, uint64_t last_usable,
                          const uint8_t disk_guid[16], uint64_t entries_lba,
                          uint32_t entries_crc) {
    zero_bytes(header, block_size);
    copy_bytes(header, "EFI PART", 8U);
    write_le32(header + 8U, 0x00010000U);
    write_le32(header + 12U, GPT_HEADER_SIZE);
    write_le32(header + 16U, 0U);
    write_le32(header + 20U, 0U);
    write_le64(header + 24U, current_lba);
    write_le64(header + 32U, backup_lba);
    write_le64(header + 40U, first_usable);
    write_le64(header + 48U, last_usable);
    copy_bytes(header + 56U, disk_guid, 16U);
    write_le64(header + 72U, entries_lba);
    write_le32(header + 80U, NEXUS_GPT_IMAGE_ENTRY_COUNT);
    write_le32(header + 84U, NEXUS_GPT_IMAGE_ENTRY_SIZE);
    write_le32(header + 88U, entries_crc);
    write_le32(header + 16U, crc32_bytes(header, GPT_HEADER_SIZE));
}

bool nexus_gpt_image_build(const struct nexus_partition_plan *plan,
                           const uint8_t disk_guid[16],
                           const uint8_t partition_guids[NEXUS_PLAN_MAX_PARTITIONS][16],
                           struct nexus_gpt_image_buffers *out) {
    uint64_t entry_blocks;
    uint64_t backup_entries_lba;
    uint64_t backup_header_lba;
    uint64_t disk_blocks;
    uint32_t block_size;
    uint32_t entries_crc;
    uint32_t protective_size;

    if (out == NULL || !validate_plan(plan, disk_guid, partition_guids))
        return false;
    block_size = plan->logical_block_size;
    disk_blocks = plan->disk_block_count;
    entry_blocks = (NEXUS_GPT_IMAGE_ENTRY_BYTES + block_size - 1U) /
                   block_size;
    backup_header_lba = disk_blocks - 1U;
    if (backup_header_lba <= entry_blocks) return false;
    backup_entries_lba = backup_header_lba - entry_blocks;
    if (plan->last_usable_lba >= backup_entries_lba) return false;

    zero_bytes(out, sizeof(*out));
    for (size_t i = 0U; i < plan->partition_count; ++i) {
        encode_partition_entry(out->primary_entries +
                                   i * NEXUS_GPT_IMAGE_ENTRY_SIZE,
                               &plan->partitions[i], partition_guids[i]);
    }
    copy_bytes(out->backup_entries, out->primary_entries,
               NEXUS_GPT_IMAGE_ENTRY_BYTES);
    entries_crc = crc32_bytes(out->primary_entries,
                              NEXUS_GPT_IMAGE_ENTRY_BYTES);

    /* Protective MBR: one 0xEE partition covering the disk after LBA 0. */
    out->protective_mbr[446U + 1U] = 0x00U;
    out->protective_mbr[446U + 2U] = 0x02U;
    out->protective_mbr[446U + 3U] = 0x00U;
    out->protective_mbr[446U + 4U] = GPT_PROTECTIVE_TYPE;
    out->protective_mbr[446U + 5U] = 0xFFU;
    out->protective_mbr[446U + 6U] = 0xFFU;
    out->protective_mbr[446U + 7U] = 0xFFU;
    write_le32(out->protective_mbr + 446U + 8U, 1U);
    protective_size = disk_blocks - 1U > UINT32_MAX
        ? UINT32_MAX : (uint32_t)(disk_blocks - 1U);
    write_le32(out->protective_mbr + 446U + 12U, protective_size);
    out->protective_mbr[510U] = 0x55U;
    out->protective_mbr[511U] = 0xAAU;

    encode_header(out->primary_header, block_size, 1U, backup_header_lba,
                  plan->first_usable_lba, plan->last_usable_lba, disk_guid,
                  GPT_PRIMARY_ENTRIES_LBA, entries_crc);
    encode_header(out->backup_header, block_size, backup_header_lba, 1U,
                  plan->first_usable_lba, plan->last_usable_lba, disk_guid,
                  backup_entries_lba, entries_crc);
    return true;
}
