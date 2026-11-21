#include "fat32.h"

#include <stddef.h>
#include <stdint.h>

#define FAT32_MAX_SECTOR_SIZE 4096U
#define FAT32_MAX_SECTORS_PER_CLUSTER 128U
#define FAT32_EOC_MIN 0x0FFFFFF8U
#define FAT32_BAD_CLUSTER 0x0FFFFFF7U
#define FAT32_ATTR_DIRECTORY 0x10U
#define FAT32_ATTR_VOLUME_ID 0x08U

static struct nexus_fat32_entry root_entries[NEXUS_FAT32_MAX_ROOT_ENTRIES];
static size_t root_entry_count;
static uint8_t sector_buffer[FAT32_MAX_SECTOR_SIZE];
static uint32_t fat_start;
static uint32_t data_start;
static uint32_t sectors_per_cluster;
static uint32_t cluster_count;
static uint32_t root_cluster;
static uint64_t partition_start;
static uint64_t partition_sectors;
static const struct nexus_block_device *mounted_device;

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool power_of_two(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

static bool read_volume_sector(uint32_t relative_lba, uint8_t *buffer) {
    if (mounted_device == NULL || relative_lba >= partition_sectors)
        return false;
    return blockdev_read(mounted_device, partition_start + relative_lba,
                         1U, buffer);
}

static bool fat_next_cluster(uint32_t cluster, uint32_t *next) {
    const uint64_t fat_byte = (uint64_t)cluster * 4U;
    const uint32_t sector = fat_start +
        (uint32_t)(fat_byte / mounted_device->block_size);
    const uint32_t offset = (uint32_t)(fat_byte % mounted_device->block_size);
    if (offset + 4U > mounted_device->block_size ||
        !read_volume_sector(sector, sector_buffer)) return false;
    *next = le32(sector_buffer + offset) & 0x0FFFFFFFU;
    return true;
}

static void decode_short_name(const uint8_t *raw, char name[13]) {
    size_t out = 0;
    size_t base_end = 8U;
    while (base_end > 0U && raw[base_end - 1U] == ' ') --base_end;
    for (size_t i = 0; i < base_end && out < 12U; ++i)
        name[out++] = (char)raw[i];
    size_t ext_end = 3U;
    while (ext_end > 0U && raw[8U + ext_end - 1U] == ' ') --ext_end;
    if (ext_end > 0U && out < 12U) name[out++] = '.';
    for (size_t i = 0; i < ext_end && out < 12U; ++i)
        name[out++] = (char)raw[8U + i];
    name[out] = '\0';
}

static bool add_directory_entry(const uint8_t *raw) {
    if (raw[0] == 0x00U) return false;
    if (raw[0] == 0xE5U || raw[11] == 0x0FU ||
        (raw[11] & FAT32_ATTR_VOLUME_ID) != 0U) return true;
    if (root_entry_count >= NEXUS_FAT32_MAX_ROOT_ENTRIES) return false;

    struct nexus_fat32_entry *entry = &root_entries[root_entry_count++];
    decode_short_name(raw, entry->name);
    entry->attributes = raw[11];
    entry->first_cluster =
        ((uint32_t)le16(raw + 20U) << 16) | le16(raw + 26U);
    entry->size = le32(raw + 28U);
    return true;
}

bool fat32_mount(const struct nexus_block_device *device,
                 const struct nexus_gpt_partition *partition) {
    mounted_device = NULL;
    root_entry_count = 0;
    if (device == NULL || partition == NULL ||
        (device->block_size != 512U && device->block_size != 4096U) ||
        partition->first_lba > partition->last_lba ||
        partition->last_lba >= device->block_count) return false;

    mounted_device = device;
    partition_start = partition->first_lba;
    partition_sectors = partition->last_lba - partition->first_lba + 1U;
    if (!read_volume_sector(0U, sector_buffer)) goto fail;
    const uint16_t bytes_per_sector = le16(sector_buffer + 11U);
    const uint8_t spc = sector_buffer[13U];
    const uint16_t reserved = le16(sector_buffer + 14U);
    const uint8_t fats = sector_buffer[16U];
    const uint16_t root_entries = le16(sector_buffer + 17U);
    const uint16_t total16 = le16(sector_buffer + 19U);
    const uint16_t fat16 = le16(sector_buffer + 22U);
    const uint32_t total32 = le32(sector_buffer + 32U);
    const uint32_t fat32 = le32(sector_buffer + 36U);
    const uint32_t root = le32(sector_buffer + 44U);

    if (sector_buffer[510U] != 0x55U || sector_buffer[511U] != 0xAAU ||
        bytes_per_sector != device->block_size ||
        !power_of_two(spc) || spc > FAT32_MAX_SECTORS_PER_CLUSTER ||
        reserved == 0U || (fats != 1U && fats != 2U) ||
        root_entries != 0U || total16 != 0U || fat16 != 0U ||
        total32 == 0U || fat32 == 0U || root < 2U ||
        total32 > partition_sectors) goto fail;

    sectors_per_cluster = spc;
    fat_start = reserved;
    const uint64_t data_start64 =
        (uint64_t)reserved + (uint64_t)fats * fat32;
    if (data_start64 >= total32) goto fail;
    data_start = (uint32_t)data_start64;
    const uint32_t data_sectors = total32 - data_start;
    cluster_count = data_sectors / sectors_per_cluster;
    if (cluster_count < 65525U || cluster_count > 0x0FFFFFF5U ||
        root >= cluster_count + 2U ||
        (uint64_t)fat32 * device->block_size / 4U < cluster_count + 2U)
        goto fail;
    root_cluster = root;

    uint32_t cluster = root_cluster;
    for (uint32_t chain = 0; chain < cluster_count; ++chain) {
        if (cluster < 2U || cluster >= cluster_count + 2U) goto fail;
        const uint64_t first_sector = (uint64_t)data_start +
            (uint64_t)(cluster - 2U) * sectors_per_cluster;
        for (uint32_t s = 0; s < sectors_per_cluster; ++s) {
            if (first_sector + s >= total32 ||
                !read_volume_sector((uint32_t)(first_sector + s),
                                    sector_buffer)) goto fail;
            for (uint32_t offset = 0; offset + 32U <= device->block_size;
                 offset += 32U) {
                if (!add_directory_entry(sector_buffer + offset)) {
                    if (sector_buffer[offset] == 0x00U) {
                        mounted_device = device;
                        return true;
                    }
                    goto fail;
                }
            }
        }
        uint32_t next = 0;
        if (!fat_next_cluster(cluster, &next)) goto fail;
        if (next >= FAT32_EOC_MIN) return true;
        if (next == cluster || next == 0U || next == 1U ||
            next == FAT32_BAD_CLUSTER) goto fail;
        cluster = next;
    }

fail:
    mounted_device = NULL;
    root_entry_count = 0;
    return false;
}

size_t fat32_root_entry_count(void) {
    return root_entry_count;
}

const struct nexus_fat32_entry *fat32_root_entry_at(size_t index) {
    if (index >= root_entry_count) return NULL;
    return &root_entries[index];
}
