#include "mbr.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DISK_BLOCKS 10000U
static uint8_t disk[DISK_BLOCKS][512U];
static struct nexus_block_device test_device;

bool blockdev_read(const struct nexus_block_device *device,
                   uint64_t lba, uint32_t blocks, void *buffer) {
    if (device != &test_device || buffer == NULL || blocks == 0U ||
        lba >= DISK_BLOCKS || blocks > DISK_BLOCKS - lba)
        return false;
    memcpy(buffer, disk[lba], (size_t)blocks * 512U);
    return true;
}

static void make_signature(uint8_t *sector) {
    sector[510] = 0x55U;
    sector[511] = 0xAAU;
}

static void set_entry(uint8_t *sector, size_t index, uint8_t status,
                      uint8_t type, uint32_t first, uint32_t count) {
    uint8_t *entry = sector + 446U + index * 16U;
    memset(entry, 0, 16U);
    entry[0] = status;
    entry[4] = type;
    entry[8] = (uint8_t)first;
    entry[9] = (uint8_t)(first >> 8);
    entry[10] = (uint8_t)(first >> 16);
    entry[11] = (uint8_t)(first >> 24);
    entry[12] = (uint8_t)count;
    entry[13] = (uint8_t)(count >> 8);
    entry[14] = (uint8_t)(count >> 16);
    entry[15] = (uint8_t)(count >> 24);
}

static void reset_disk(void) {
    memset(disk, 0, sizeof(disk));
    test_device = (struct nexus_block_device){
        .block_count = DISK_BLOCKS,
        .block_size = 512U,
        .max_blocks_per_request = 128U,
        .flags = NEXUS_BLOCKDEV_READ_ONLY,
        .transport = NEXUS_BLOCKDEV_TRANSPORT_AHCI,
        .read = NULL
    };
}

static void test_primary_and_extended_partitions(void) {
    reset_disk();
    disk[0][440] = 0x78U;
    disk[0][441] = 0x56U;
    disk[0][442] = 0x34U;
    disk[0][443] = 0x12U;
    set_entry(disk[0], 0U, 0x80U, 0x0CU, 2048U, 2048U);
    set_entry(disk[0], 1U, 0x00U, 0x0FU, 4096U, 4000U);
    make_signature(disk[0]);

    set_entry(disk[4096U], 0U, 0x00U, 0x0BU, 63U, 1024U);
    set_entry(disk[4096U], 1U, 0x00U, 0x0FU, 2048U, 1900U);
    make_signature(disk[4096U]);

    set_entry(disk[6144U], 0U, 0x00U, 0x0CU, 63U, 512U);
    make_signature(disk[6144U]);

    assert(mbr_scan_device(&test_device));
    assert(!mbr_has_protective_gpt());
    assert(mbr_disk_signature() == 0x12345678U);
    assert(mbr_partition_count() == 4U);

    const struct nexus_mbr_partition *p = mbr_partition_at(0U);
    assert(p != NULL && p->type == 0x0CU && p->bootable);
    assert(!p->logical && p->first_lba == 2048U && p->block_count == 2048U);
    p = mbr_partition_at(1U);
    assert(p != NULL && p->type == 0x0FU && !p->logical);
    assert(p->first_lba == 4096U && p->block_count == 4000U);
    p = mbr_partition_at(2U);
    assert(p != NULL && p->logical && p->type == 0x0BU);
    assert(p->first_lba == 4159U && p->block_count == 1024U);
    p = mbr_partition_at(3U);
    assert(p != NULL && p->logical && p->type == 0x0CU);
    assert(p->first_lba == 6207U && p->block_count == 512U);
    assert(mbr_type_is_fat32(0x0BU));
    assert(mbr_type_is_fat32(0x0CU));
    assert(!mbr_type_is_fat32(0x07U));
    assert(mbr_type_is_extended(0x05U));
    assert(mbr_type_is_extended(0x0FU));
    assert(mbr_type_is_extended(0x85U));
    assert(!mbr_type_is_extended(0x07U));
}

static void test_protective_gpt_and_bad_signature(void) {
    reset_disk();
    set_entry(disk[0], 0U, 0x00U, 0xEEU, 1U, DISK_BLOCKS - 1U);
    make_signature(disk[0]);
    assert(!mbr_scan_device(&test_device));
    assert(mbr_has_protective_gpt());
    assert(mbr_partition_count() == 0U);

    memset(disk[0], 0, sizeof(disk[0]));
    assert(!mbr_scan_device(&test_device));
    assert(!mbr_has_protective_gpt());
}

static void test_invalid_primary_range_and_status(void) {
    reset_disk();
    set_entry(disk[0], 0U, 0x7FU, 0x0CU, 100U, 200U);
    make_signature(disk[0]);
    assert(!mbr_scan_device(&test_device));

    reset_disk();
    set_entry(disk[0], 0U, 0x00U, 0x0CU,
              DISK_BLOCKS - 20U, 100U);
    make_signature(disk[0]);
    assert(!mbr_scan_device(&test_device));
}

static void test_ebr_self_loop_is_rejected(void) {
    reset_disk();
    set_entry(disk[0], 0U, 0x00U, 0x0FU, 4096U, 3000U);
    make_signature(disk[0]);
    set_entry(disk[4096U], 0U, 0x00U, 0x0BU, 63U, 512U);
    set_entry(disk[4096U], 1U, 0x00U, 0x0FU, 2048U, 2000U);
    make_signature(disk[4096U]);
    set_entry(disk[6144U], 0U, 0x00U, 0x0CU, 63U, 256U);
    /* The link points back to the same EBR, creating a cycle. */
    set_entry(disk[6144U], 1U, 0x00U, 0x0FU, 2048U, 2000U);
    make_signature(disk[6144U]);
    assert(!mbr_scan_device(&test_device));
    assert(mbr_partition_count() == 0U);
}

static void test_missing_or_invalid_ebr_is_rejected(void) {
    reset_disk();
    set_entry(disk[0], 0U, 0x00U, 0x0FU, 4096U, 3000U);
    make_signature(disk[0]);
    /* The extended partition points at an EBR without a valid signature. */
    assert(!mbr_scan_device(&test_device));
    assert(mbr_partition_count() == 0U);
}

int main(void) {
    test_primary_and_extended_partitions();
    test_protective_gpt_and_bad_signature();
    test_invalid_primary_range_and_status();
    test_ebr_self_loop_is_rejected();
    test_missing_or_invalid_ebr_is_rejected();
    puts("NEXUS MBR/EBR PARTITION TESTS PASSED");
    return 0;
}
