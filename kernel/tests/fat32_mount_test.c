#include "fat32.h"
#include "blockdev.h"
#include "serial.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TEST_PARTITION_START 10U
#define TEST_PARTITION_SECTORS 70000U
#define TEST_VOLUME_SECTORS 66069U
#define TEST_RESERVED_SECTORS 32U
#define TEST_FAT_SECTORS 512U
#define TEST_DATA_START (TEST_RESERVED_SECTORS + TEST_FAT_SECTORS)
#define TEST_CLUSTER_COUNT (TEST_VOLUME_SECTORS - TEST_DATA_START)
#define TEST_LAST_CLUSTER (TEST_CLUSTER_COUNT + 1U)
#define TEST_DISK_SECTORS (TEST_PARTITION_START + TEST_PARTITION_SECTORS)

static uint8_t disk[TEST_DISK_SECTORS][512U];
static uint64_t highest_read_lba;

static void put16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
static void put32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static bool mock_read(void *context, uint64_t lba, uint32_t count,
                      void *buffer) {
    (void)context;
    if (count == 0U || lba >= TEST_DISK_SECTORS ||
        count > TEST_DISK_SECTORS - lba) return false;
    if (lba + count - 1U > highest_read_lba)
        highest_read_lba = lba + count - 1U;
    memcpy(buffer, &disk[lba][0], (size_t)count * 512U);
    return true;
}

bool blockdev_read(const struct nexus_block_device *device,
                   uint64_t lba, uint32_t count, void *buffer) {
    return device != NULL && device->read != NULL &&
           device->read(device->context, lba, count, buffer);
}
void serial_write(const char *text) { (void)text; }
void serial_write_hex(uint64_t value) { (void)value; }

static struct nexus_block_device test_device(void) {
    struct nexus_block_device device = {0};
    device.block_count = TEST_DISK_SECTORS;
    device.block_size = 512U;
    device.max_blocks_per_request = 1U;
    device.read = mock_read;
    return device;
}

static struct nexus_gpt_partition test_partition(void) {
    struct nexus_gpt_partition partition = {0};
    partition.first_lba = TEST_PARTITION_START;
    partition.last_lba = TEST_PARTITION_START + TEST_PARTITION_SECTORS - 1U;
    return partition;
}

static uint8_t test_short_checksum(const uint8_t *name) {
    uint8_t sum = 0U;
    for (size_t i = 0U; i < 11U; ++i)
        sum = (uint8_t)(((sum & 1U) ? 0x80U : 0U) + (sum >> 1) + name[i]);
    return sum;
}

static void make_lfn_entry(uint8_t *raw, const char *name,
                           const uint8_t *short_name,
                           uint8_t ordinal, uint8_t total) {
    static const uint8_t offsets[13] = {
        1U,3U,5U,7U,9U,14U,16U,18U,20U,22U,24U,28U,30U
    };
    memset(raw, 0xFF, 32U);
    raw[0] = ordinal | (ordinal == total ? 0x40U : 0U);
    raw[11] = 0x0FU;
    raw[12] = 0U;
    raw[13] = test_short_checksum(short_name);
    put16(raw + 26U, 0U);
    const size_t base = (size_t)(ordinal - 1U) * 13U;
    for (size_t i = 0U; i < 13U; ++i) {
        const unsigned char ch = (unsigned char)name[base + i];
        put16(raw + offsets[i], ch == 0U ? 0U : ch);
        if (ch == 0U) {
            for (size_t j = i + 1U; j < 13U; ++j)
                put16(raw + offsets[j], 0xFFFFU);
            break;
        }
    }
}

static void format_minimal_fat32(void) {
    memset(disk, 0, sizeof(disk));
    uint8_t *boot = disk[TEST_PARTITION_START];
    put16(boot + 11U, 512U);
    boot[13U] = 1U;
    put16(boot + 14U, TEST_RESERVED_SECTORS);
    boot[16U] = 1U;
    put32(boot + 32U, TEST_VOLUME_SECTORS);
    put32(boot + 36U, TEST_FAT_SECTORS);
    put32(boot + 44U, 2U);
    boot[510U] = 0x55U;
    boot[511U] = 0xAAU;

    /* Forty root entries span three clusters, beyond the old 32-entry cap.
     * The first file points at the final valid data cluster. */
    for (uint32_t cluster = 2U; cluster <= 4U; ++cluster) {
        const uint64_t fat_byte = (uint64_t)cluster * 4U;
        const uint32_t fat_sector = TEST_RESERVED_SECTORS +
            (uint32_t)(fat_byte / 512U);
        const uint32_t fat_offset = (uint32_t)(fat_byte % 512U);
        const uint32_t next = cluster == 4U ? 0x0FFFFFFFU : cluster + 1U;
        put32(disk[TEST_PARTITION_START + fat_sector] + fat_offset, next);
    }
    const uint64_t final_fat_byte = (uint64_t)TEST_LAST_CLUSTER * 4U;
    const uint32_t final_fat_sector = TEST_RESERVED_SECTORS +
        (uint32_t)(final_fat_byte / 512U);
    const uint32_t final_fat_offset = (uint32_t)(final_fat_byte % 512U);
    put32(disk[TEST_PARTITION_START + final_fat_sector] + final_fat_offset,
          0x0FFFFFFFU);

    for (uint32_t index = 0U; index < 40U; ++index) {
        const uint32_t physical_slot = index + (index >= 2U ? 2U : 0U);
        const uint32_t cluster = 2U + physical_slot / 16U;
        const uint32_t slot = physical_slot % 16U;
        uint8_t *root = disk[TEST_PARTITION_START + TEST_DATA_START +
                             (cluster - 2U)] + slot * 32U;
        if (index == 1U) {
            uint8_t short_name[11];
            memcpy(short_name, "LONGFI~1TXT", 11U);
            uint8_t *lfn_first = root;
            uint8_t *lfn_second = disk[TEST_PARTITION_START + TEST_DATA_START +
                (cluster - 2U)] + (slot + 1U) * 32U;
            uint8_t *short_entry = disk[TEST_PARTITION_START + TEST_DATA_START +
                (cluster - 2U)] + (slot + 2U) * 32U;
            make_lfn_entry(lfn_first, "Long Filename.txt", short_name, 2U, 2U);
            make_lfn_entry(lfn_second, "Long Filename.txt", short_name, 1U, 2U);
            memcpy(short_entry, short_name, 11U);
            short_entry[11U] = 0x20U;
            continue;
        }
        if (index == 0U) {
            memcpy(root, "BOUNDARY BIN", 12U);
            root[11U] = 0x20U;
            put16(root + 20U, (uint16_t)(TEST_LAST_CLUSTER >> 16));
            put16(root + 26U, (uint16_t)TEST_LAST_CLUSTER);
            put32(root + 28U, 512U);
        } else {
            memcpy(root, "FILE0001TXT", 11U);
            root[6U] = (uint8_t)('0' + (index / 10U));
            root[7U] = (uint8_t)('0' + (index % 10U));
            root[11U] = 0x20U;
        }
    }
    /* Mark the first unused slot so enumeration terminates normally. */
    disk[TEST_PARTITION_START + TEST_DATA_START + 2U][10U * 32U] = 0U;

    uint8_t *file_data =
        disk[TEST_PARTITION_START + TEST_VOLUME_SECTORS - 1U];
    for (size_t i = 0U; i < 512U; ++i)
        file_data[i] = (uint8_t)(i ^ 0xA5U);
}

int main(void) {
    struct nexus_block_device device = test_device();
    struct nexus_gpt_partition partition = test_partition();
    uint8_t output[512U];

    format_minimal_fat32();
    highest_read_lba = 0U;
    assert(fat32_mount(&device, &partition));
    assert(fat32_root_entry_count() == 40U);
    assert(strcmp(fat32_root_entry_at(1U)->name, "Long Filename.txt") == 0);
    const struct nexus_fat32_entry *entry = fat32_root_entry_at(0U);
    assert(entry != NULL);
    assert(entry->first_cluster == TEST_LAST_CLUSTER);
    assert(fat32_read_file(entry, 0U, sizeof(output), output));
    for (size_t i = 0U; i < sizeof(output); ++i)
        assert(output[i] == (uint8_t)(i ^ 0xA5U));
    assert(highest_read_lba <=
           TEST_PARTITION_START + TEST_VOLUME_SECTORS - 1U);

    /* A damaged VFAT checksum must never rename the short directory entry. */
    uint8_t *lfn_record = disk[TEST_PARTITION_START + TEST_DATA_START +
                               3U * 32U];
    lfn_record[13U] ^= 0x01U;
    assert(fat32_mount(&device, &partition));
    assert(strcmp(fat32_root_entry_at(1U)->name, "LONGFI~1.TXT") == 0);
    lfn_record[13U] ^= 0x01U;

    /* A deleted LFN slot invalidates the sequence before the short entry. */
    lfn_record[0U] = 0xE5U;
    assert(fat32_mount(&device, &partition));
    assert(strcmp(fat32_root_entry_at(1U)->name, "LONGFI~1.TXT") == 0);
    lfn_record[0U] = 0x42U;

    /* A partition may be larger than its BPB-declared volume; the parser
     * must use the BPB volume boundary for all data-sector reads. */
    partition.last_lba = TEST_PARTITION_START + TEST_PARTITION_SECTORS - 1U;
    assert(fat32_mount(&device, &partition));

    /* Reject a volume declaration extending beyond its containing partition. */
    put32(disk[TEST_PARTITION_START] + 32U,
          TEST_PARTITION_SECTORS + 1U);
    assert(!fat32_mount(&device, &partition));
    assert(fat32_root_entry_count() == 0U);
    return 0;
}
