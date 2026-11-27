#include "mbr.h"

#include <stddef.h>
#include <stdint.h>

#define MBR_PARTITION_TABLE_OFFSET 446U
#define MBR_PARTITION_ENTRY_BYTES 16U
#define MBR_SIGNATURE_OFFSET 510U
#define MBR_SIGNATURE_A 0x55U
#define MBR_SIGNATURE_B 0xAAU
#define MBR_PROTECTIVE_TYPE 0xEEU
#define MBR_EBR_ENTRY_DATA 0U
#define MBR_EBR_ENTRY_LINK 1U

static struct nexus_mbr_partition partitions[NEXUS_MBR_MAX_PARTITIONS];
static size_t partition_count;
static bool protective_gpt;
static uint32_t disk_signature;

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool sector_signature_valid(const uint8_t *sector) {
    return sector != NULL &&
        sector[MBR_SIGNATURE_OFFSET] == MBR_SIGNATURE_A &&
        sector[MBR_SIGNATURE_OFFSET + 1U] == MBR_SIGNATURE_B;
}

bool mbr_type_is_extended(uint8_t type) {
    return type == 0x05U || type == 0x0FU || type == 0x85U;
}

bool mbr_type_is_fat32(uint8_t type) {
    return type == 0x0BU || type == 0x0CU;
}

static bool append_partition(uint8_t type, bool bootable, bool logical,
                              uint64_t first_lba, uint64_t block_count,
                              uint64_t disk_blocks,
                              uint64_t container_first,
                              uint64_t container_end) {
    if (type == 0U || block_count == 0U ||
        first_lba >= disk_blocks || block_count > disk_blocks - first_lba ||
        partition_count >= NEXUS_MBR_MAX_PARTITIONS)
        return false;
    if (logical &&
        (first_lba < container_first || first_lba >= container_end ||
         block_count > container_end - first_lba))
        return false;
    const uint64_t last_lba = first_lba + block_count;
    for (size_t i = 0U; i < partition_count; ++i) {
        const struct nexus_mbr_partition *prior = &partitions[i];
        const bool prior_is_container =
            !prior->logical && mbr_type_is_extended(prior->type);
        if (!logical && prior->logical) continue;
        if (logical && prior_is_container) continue;
        const uint64_t prior_end = prior->first_lba + prior->block_count;
        if (first_lba < prior_end && prior->first_lba < last_lba)
            return false;
    }
    partitions[partition_count++] = (struct nexus_mbr_partition){
        .type = type,
        .bootable = bootable,
        .logical = logical,
        .first_lba = first_lba,
        .block_count = block_count
    };
    return true;
}

static bool read_entry(const uint8_t *sector, size_t index,
                       uint8_t *status, uint8_t *type,
                       uint32_t *first, uint32_t *count) {
    if (sector == NULL || index >= 4U ||
        status == NULL || type == NULL || first == NULL || count == NULL)
        return false;
    const uint8_t *entry = sector + MBR_PARTITION_TABLE_OFFSET +
                           index * MBR_PARTITION_ENTRY_BYTES;
    *status = entry[0];
    *type = entry[4];
    *first = read_le32(entry + 8U);
    *count = read_le32(entry + 12U);
    if (*status != 0x00U && *status != 0x80U) return false;
    if (*type == 0U) return *first == 0U && *count == 0U;
    return *first != 0U && *count != 0U;
}

static bool scan_extended(const struct nexus_block_device *device,
                          uint64_t base, uint64_t count) {
    uint8_t sector[4096];
    uint64_t visited[NEXUS_MBR_MAX_EBR_HOPS];
    size_t visited_count = 0U;
    if (base >= device->block_count || count == 0U ||
        count > device->block_count - base)
        return false;
    const uint64_t end = base + count;

    uint64_t ebr_lba = base;
    for (size_t hop = 0U; hop < NEXUS_MBR_MAX_EBR_HOPS; ++hop) {
        if (ebr_lba < base || ebr_lba >= end) return false;
        for (size_t i = 0U; i < visited_count; ++i)
            if (visited[i] == ebr_lba) return false;
        visited[visited_count++] = ebr_lba;

        if (!blockdev_read(device, ebr_lba, 1U, sector) ||
            !sector_signature_valid(sector))
            return false;

        uint8_t status0, type0, status1, type1;
        uint8_t status2, type2, status3, type3;
        uint32_t first0, count0, first1, count1;
        uint32_t first2, count2, first3, count3;
        if (!read_entry(sector, MBR_EBR_ENTRY_DATA,
                        &status0, &type0, &first0, &count0) ||
            !read_entry(sector, MBR_EBR_ENTRY_LINK,
                        &status1, &type1, &first1, &count1) ||
            !read_entry(sector, 2U, &status2, &type2, &first2, &count2) ||
            !read_entry(sector, 3U, &status3, &type3, &first3, &count3) ||
            type2 != 0U || type3 != 0U)
            return false;

        if (type0 != 0U) {
            const uint64_t logical_first = ebr_lba + first0;
            if (logical_first < ebr_lba || !append_partition(
                    type0, status0 == 0x80U, true,
                    logical_first, count0, device->block_count, base, end))
                return false;
        }

        if (type1 == 0U) return true;
        if (!mbr_type_is_extended(type1) || status1 != 0U || first1 == 0U)
            return false;
        const uint64_t next_ebr = base + first1;
        if (next_ebr < base || next_ebr >= end || count1 == 0U ||
            count1 > end - next_ebr)
            return false;
        ebr_lba = next_ebr;
    }
    /* An EBR chain exceeding the bound is rejected, never followed forever. */
    return false;
}

bool mbr_scan_device(const struct nexus_block_device *device) {
    partition_count = 0U;
    protective_gpt = false;
    disk_signature = 0U;
    for (size_t i = 0U; i < NEXUS_MBR_MAX_PARTITIONS; ++i)
        partitions[i] = (struct nexus_mbr_partition){0};

    if (device == NULL || device->block_count < 2U ||
        (device->block_size != 512U && device->block_size != 4096U))
        return false;

    uint8_t sector[4096];
    if (!blockdev_read(device, 0U, 1U, sector) ||
        !sector_signature_valid(sector))
        return false;
    disk_signature = read_le32(sector + 440U);

    uint64_t extended_base = 0U;
    uint64_t extended_count = 0U;
    for (size_t i = 0U; i < 4U; ++i) {
        uint8_t status, type;
        uint32_t first, count;
        if (!read_entry(sector, i, &status, &type, &first, &count))
            return false;
        if (type == MBR_PROTECTIVE_TYPE) {
            protective_gpt = true;
            continue;
        }
        if (type == 0U) continue;
        if (mbr_type_is_extended(type)) {
            if (extended_count != 0U) return false;
            extended_base = first;
            extended_count = count;
        }
        if (!append_partition(type, status == 0x80U, false, first,
                               count, device->block_count, 0U,
                               device->block_count))
            return false;
    }

    if (protective_gpt) {
        /* A protective MBR is not treated as a usable legacy partition map. */
        partition_count = 0U;
        return false;
    }
    if (extended_count != 0U &&
        !scan_extended(device, extended_base, extended_count)) {
        partition_count = 0U;
        return false;
    }
    return partition_count != 0U;
}

bool mbr_has_protective_gpt(void) {
    return protective_gpt;
}

uint32_t mbr_disk_signature(void) {
    return disk_signature;
}

size_t mbr_partition_count(void) {
    return partition_count;
}

const struct nexus_mbr_partition *mbr_partition_at(size_t index) {
    return index < partition_count ? &partitions[index] : NULL;
}
