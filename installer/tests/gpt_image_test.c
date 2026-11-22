#include "gpt_image.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p) {
    return (uint64_t)read_le32(p) |
           ((uint64_t)read_le32(p + 4U) << 32);
}

static uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

static void check_header(const uint8_t *header, uint32_t block_size,
                         uint64_t current, uint64_t backup,
                         uint64_t entries_lba) {
    uint8_t copy[NEXUS_GPT_IMAGE_MAX_SECTOR];
    assert(header[0] == 'E' && header[1] == 'F' &&
           header[2] == 'I' && header[3] == ' ');
    assert(read_le32(header + 8U) == 0x00010000U);
    assert(read_le32(header + 12U) == 92U);
    assert(read_le64(header + 24U) == current);
    assert(read_le64(header + 32U) == backup);
    assert(read_le64(header + 72U) == entries_lba);
    assert(read_le32(header + 80U) == 128U);
    assert(read_le32(header + 84U) == 128U);
    for (uint32_t i = 0U; i < block_size; ++i) copy[i] = header[i];
    const uint32_t expected = read_le32(copy + 16U);
    copy[16] = copy[17] = copy[18] = copy[19] = 0U;
    assert(crc32(copy, 92U) == expected);
}

static void fill_guids(uint8_t disk[16], uint8_t parts[5][16]) {
    for (size_t i = 0U; i < 16U; ++i) {
        disk[i] = (uint8_t)(0xA0U + i);
        for (size_t p = 0U; p < 5U; ++p)
            parts[p][i] = (uint8_t)(0x10U + p * 16U + i);
    }
}

static void test_uefi_image(void) {
    struct nexus_partition_plan plan;
    struct nexus_gpt_image_buffers image;
    uint8_t disk_guid[16], partition_guids[5][16];
    fill_guids(disk_guid, partition_guids);
    assert(nexus_partition_plan_create(64ULL * 1024ULL * 1024ULL * 1024ULL /
                                       512ULL, 512U,
                                       NEXUS_FIRMWARE_UEFI, &plan));
    assert(nexus_gpt_image_build(&plan, disk_guid, &partition_guids[0][0], &image));
    assert(image.protective_mbr[510] == 0x55U &&
           image.protective_mbr[511] == 0xAAU);
    assert(image.protective_mbr[450] == 0xEEU);
    assert(read_le32(image.protective_mbr + 454U) == 1U);
    check_header(image.primary_header, 512U, 1U,
                 plan.disk_block_count - 1U, 2U);
    check_header(image.backup_header, 512U, plan.disk_block_count - 1U,
                 1U, plan.disk_block_count - 1U - 32U);
    assert(read_le32(image.primary_header + 88U) ==
           crc32(image.primary_entries, NEXUS_GPT_IMAGE_ENTRY_BYTES));
    for (size_t i = 0U; i < NEXUS_GPT_IMAGE_ENTRY_BYTES; ++i)
        assert(image.primary_entries[i] == image.backup_entries[i]);
    assert(image.primary_entries[0] == 0x28U); /* EFI System GUID */
    assert(image.primary_entries[128] == 0x46U); /* Nexus system NXFS */
    assert(image.primary_entries[256] == 0x4EU); /* Nexus recovery */
    assert(image.primary_entries[384] == 0x46U); /* Nexus data NXFS */
}

static void test_dual_firmware_image(void) {
    struct nexus_partition_plan plan;
    struct nexus_gpt_image_buffers image;
    uint8_t disk_guid[16], partition_guids[5][16];
    fill_guids(disk_guid,partition_guids);
    assert(nexus_partition_plan_create(64ULL*1024ULL*1024ULL*1024ULL/512ULL,
        512U,NEXUS_FIRMWARE_DUAL,&plan));
    assert(nexus_gpt_image_build(&plan,disk_guid,&partition_guids[0][0],&image));
    assert(image.primary_entries[0]==0x28U);
    assert(image.primary_entries[128]=='H');
    assert(image.primary_entries[256]==0x46U);
    assert(image.primary_entries[384]==0x4EU);
    assert(image.primary_entries[512]==0x46U);
    for(size_t i=0U;i<NEXUS_GPT_IMAGE_ENTRY_BYTES;i++)
        assert(image.primary_entries[i]==image.backup_entries[i]);
}

static void test_4kn_image(void) {
    struct nexus_partition_plan plan;
    struct nexus_gpt_image_buffers image;
    uint8_t disk_guid[16], partition_guids[5][16];
    fill_guids(disk_guid, partition_guids);
    assert(nexus_partition_plan_create(64ULL * 1024ULL * 1024ULL * 1024ULL /
                                       4096ULL, 4096U,
                                       NEXUS_FIRMWARE_UEFI, &plan));
    assert(nexus_gpt_image_build(&plan, disk_guid, &partition_guids[0][0], &image));
    check_header(image.primary_header, 4096U, 1U,
                 plan.disk_block_count - 1U, 2U);
    check_header(image.backup_header, 4096U, plan.disk_block_count - 1U,
                 1U, plan.disk_block_count - 1U - 4U);
}

static void test_rejects_duplicate_guids_and_invalid_plan(void) {
    struct nexus_partition_plan plan;
    struct nexus_gpt_image_buffers image;
    uint8_t disk_guid[16], partition_guids[5][16];
    fill_guids(disk_guid, partition_guids);
    assert(nexus_partition_plan_create(64ULL * 1024ULL * 1024ULL * 1024ULL /
                                       512ULL, 512U,
                                       NEXUS_FIRMWARE_UEFI, &plan));
    for (size_t i = 0U; i < 16U; ++i)
        partition_guids[1][i] = partition_guids[0][i];
    assert(!nexus_gpt_image_build(&plan, disk_guid, &partition_guids[0][0], &image));
    fill_guids(disk_guid, partition_guids);
    plan.partitions[1].first_lba = plan.partitions[0].first_lba;
    assert(!nexus_gpt_image_build(&plan, disk_guid, &partition_guids[0][0], &image));
}

int main(void) {
    test_uefi_image();
    test_dual_firmware_image();
    test_4kn_image();
    test_rejects_duplicate_guids_and_invalid_plan();
    puts("NEXUS GPT IMAGE SERIALIZATION TESTS PASSED");
    return 0;
}
