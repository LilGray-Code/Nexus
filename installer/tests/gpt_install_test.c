#include "gpt_install.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

struct mock_disk {
    uint64_t block_count;
    uint32_t block_size;
    uint64_t backup_entries_lba;
    uint64_t backup_header_lba;
    uint32_t entry_blocks;
    uint8_t mbr[4096];
    uint8_t primary_header[4096];
    uint8_t backup_header[4096];
    uint8_t primary_entries[NEXUS_GPT_IMAGE_ENTRY_BYTES];
    uint8_t backup_entries[NEXUS_GPT_IMAGE_ENTRY_BYTES];
    uint32_t writes;
    uint32_t flushes;
    uint32_t fail_write_at;
    uint64_t write_lbas[5];
};

static void copy_bytes(uint8_t *to, const uint8_t *from, size_t length) {
    for (size_t i = 0U; i < length; ++i) to[i] = from[i];
}

static bool mock_write(void *context, uint64_t lba, uint32_t count,
                       const void *buffer) {
    struct mock_disk *disk = context;
    const uint8_t *bytes = buffer;
    ++disk->writes;
    if (disk->writes <= 5U) disk->write_lbas[disk->writes - 1U] = lba;
    if (disk->fail_write_at == disk->writes) return false;
    if (lba == 0U && count == 1U) {
        copy_bytes(disk->mbr, bytes, disk->block_size);
    } else if (lba == 1U && count == 1U) {
        copy_bytes(disk->primary_header, bytes, disk->block_size);
    } else if (lba == 2U && count == disk->entry_blocks) {
        copy_bytes(disk->primary_entries, bytes,
                   NEXUS_GPT_IMAGE_ENTRY_BYTES);
    } else if (lba == disk->backup_entries_lba &&
               count == disk->entry_blocks) {
        copy_bytes(disk->backup_entries, bytes,
                   NEXUS_GPT_IMAGE_ENTRY_BYTES);
    } else if (lba == disk->backup_header_lba && count == 1U) {
        copy_bytes(disk->backup_header, bytes, disk->block_size);
    } else {
        return false;
    }
    return true;
}

static bool mock_read(void *context, uint64_t lba, uint32_t count,
                      void *buffer) {
    struct mock_disk *disk = context;
    uint8_t *bytes = buffer;
    if (lba == 0U && count == 1U) {
        copy_bytes(bytes, disk->mbr, disk->block_size);
    } else if (lba == 1U && count == 1U) {
        copy_bytes(bytes, disk->primary_header, disk->block_size);
    } else if (lba == 2U && count == disk->entry_blocks) {
        copy_bytes(bytes, disk->primary_entries,
                   NEXUS_GPT_IMAGE_ENTRY_BYTES);
    } else if (lba == disk->backup_entries_lba &&
               count == disk->entry_blocks) {
        copy_bytes(bytes, disk->backup_entries,
                   NEXUS_GPT_IMAGE_ENTRY_BYTES);
    } else if (lba == disk->backup_header_lba && count == 1U) {
        copy_bytes(bytes, disk->backup_header, disk->block_size);
    } else {
        return false;
    }
    return true;
}

static bool mock_flush(void *context) {
    struct mock_disk *disk = context;
    ++disk->flushes;
    return true;
}

static void init_disk(struct mock_disk *disk, uint64_t blocks,
                      uint32_t block_size) {
    *disk = (struct mock_disk){0};
    disk->block_count = blocks;
    disk->block_size = block_size;
    disk->entry_blocks =
        (NEXUS_GPT_IMAGE_ENTRY_BYTES + block_size - 1U) / block_size;
    disk->backup_header_lba = blocks - 1U;
    disk->backup_entries_lba =
        disk->backup_header_lba - disk->entry_blocks;
}

static void fill_guids(uint8_t disk[16], uint8_t parts[4][16]) {
    for (size_t i = 0U; i < 16U; ++i) {
        disk[i] = (uint8_t)(0xA0U + i);
        for (size_t p = 0U; p < 4U; ++p)
            parts[p][i] = (uint8_t)(0x10U + p * 16U + i);
    }
}

static struct nexus_gpt_install_target target_for(struct mock_disk *disk) {
    return (struct nexus_gpt_install_target){
        .block_count = disk->block_count,
        .block_size = disk->block_size,
        .context = disk,
        .read = mock_read,
        .write = mock_write,
        .flush = mock_flush,
        .identity_revalidated = true,
        .not_live_media = true,
        .not_mounted = true
    };
}

static void test_requires_two_confirmations(void) {
    struct nexus_partition_plan plan;
    struct mock_disk disk;
    uint8_t disk_guid[16], parts[4][16];
    fill_guids(disk_guid, parts);
    assert(nexus_partition_plan_create(64ULL * 1024ULL * 1024ULL * 1024ULL /
                                       512ULL, 512U,
                                       NEXUS_FIRMWARE_UEFI, &plan));
    init_disk(&disk, plan.disk_block_count, 512U);
    struct nexus_gpt_install_target target = target_for(&disk);
    assert(nexus_gpt_install_apply(&target, &plan, disk_guid,
                                   &parts[0][0], false, true) ==
           NEXUS_GPT_INSTALL_NOT_CONFIRMED);
    assert(nexus_gpt_install_apply(&target, &plan, disk_guid,
                                   &parts[0][0], true, false) ==
           NEXUS_GPT_INSTALL_NOT_CONFIRMED);
    assert(disk.writes == 0U);
}

static void test_rejects_unsafe_targets_without_writes(void) {
    struct nexus_partition_plan plan;
    struct mock_disk disk;
    uint8_t disk_guid[16], parts[4][16];
    fill_guids(disk_guid, parts);
    assert(nexus_partition_plan_create(64ULL * 1024ULL * 1024ULL * 1024ULL /
                                       512ULL, 512U,
                                       NEXUS_FIRMWARE_UEFI, &plan));
    init_disk(&disk, plan.disk_block_count, 512U);
    struct nexus_gpt_install_target target = target_for(&disk);
    target.identity_revalidated = false;
    assert(nexus_gpt_install_apply(&target, &plan, disk_guid,
                                   &parts[0][0], true, true) ==
           NEXUS_GPT_INSTALL_UNSAFE_TARGET);
    target = target_for(&disk);
    target.not_live_media = false;
    assert(nexus_gpt_install_apply(&target, &plan, disk_guid,
                                   &parts[0][0], true, true) ==
           NEXUS_GPT_INSTALL_UNSAFE_TARGET);
    target = target_for(&disk);
    target.not_mounted = false;
    assert(nexus_gpt_install_apply(&target, &plan, disk_guid,
                                   &parts[0][0], true, true) ==
           NEXUS_GPT_INSTALL_UNSAFE_TARGET);
    assert(disk.writes == 0U);
}

static void test_writes_backup_then_primary_and_verifies(void) {
    struct nexus_partition_plan plan;
    struct nexus_gpt_image_buffers image;
    struct mock_disk disk;
    uint8_t disk_guid[16], parts[4][16];
    fill_guids(disk_guid, parts);
    assert(nexus_partition_plan_create(64ULL * 1024ULL * 1024ULL * 1024ULL /
                                       512ULL, 512U,
                                       NEXUS_FIRMWARE_UEFI, &plan));
    assert(nexus_gpt_image_build(&plan, disk_guid, &parts[0][0], &image));
    init_disk(&disk, plan.disk_block_count, 512U);
    struct nexus_gpt_install_target target = target_for(&disk);
    assert(nexus_gpt_install_apply(&target, &plan, disk_guid,
                                   &parts[0][0], true, true) ==
           NEXUS_GPT_INSTALL_OK);
    assert(disk.writes == 5U && disk.flushes == 5U);
    assert(disk.write_lbas[0] == disk.backup_entries_lba);
    assert(disk.write_lbas[1] == disk.backup_header_lba);
    assert(disk.write_lbas[2] == 2U);
    assert(disk.write_lbas[3] == 1U);
    assert(disk.write_lbas[4] == 0U);
    assert(disk.mbr[510] == 0x55U && disk.mbr[511] == 0xAAU);
    assert(disk.primary_header[0] == 'E');
    assert(disk.backup_header[0] == 'E');
    for (size_t i = 0U; i < NEXUS_GPT_IMAGE_ENTRY_BYTES; ++i) {
        assert(disk.primary_entries[i] == image.primary_entries[i]);
        assert(disk.backup_entries[i] == image.backup_entries[i]);
    }
}

static void test_injected_io_failure_stops_transaction(void) {
    for (uint32_t fail_at = 1U; fail_at <= 5U; ++fail_at) {
        struct nexus_partition_plan plan;
        struct mock_disk disk;
        uint8_t disk_guid[16], parts[4][16];
        fill_guids(disk_guid, parts);
        assert(nexus_partition_plan_create(64ULL * 1024ULL * 1024ULL * 1024ULL /
                                           512ULL, 512U,
                                           NEXUS_FIRMWARE_UEFI, &plan));
        init_disk(&disk, plan.disk_block_count, 512U);
        disk.fail_write_at = fail_at;
        struct nexus_gpt_install_target target = target_for(&disk);
        assert(nexus_gpt_install_apply(&target, &plan, disk_guid,
                                       &parts[0][0], true, true) ==
               NEXUS_GPT_INSTALL_IO_ERROR);
        assert(disk.writes == fail_at);
    }
}

int main(void) {
    test_requires_two_confirmations();
    test_rejects_unsafe_targets_without_writes();
    test_writes_backup_then_primary_and_verifies();
    test_injected_io_failure_stops_transaction();
    puts("NEXUS GPT INSTALL TRANSACTION TESTS PASSED");
    return 0;
}
