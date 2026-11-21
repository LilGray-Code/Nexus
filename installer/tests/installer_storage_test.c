#include "installer_storage.h"
#include "blockdev.h"
#include "gpt_image.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISK_BLOCKS (16ULL * 1024ULL * 1024ULL * 1024ULL / 512ULL)
#define EDGE_BLOCKS 64U
#define FORMAT_BASE 2048U
#define FORMAT_BLOCKS 4096U

struct mock_disk {
    uint8_t first[EDGE_BLOCKS * 512U];
    uint8_t last[EDGE_BLOCKS * 512U];
    uint8_t format_area[FORMAT_BLOCKS * 512U];
    uint64_t nxfs_base;
    uint8_t nxfs_area[128U * 512U];
    unsigned flushes;
};

static bool mock_range(struct mock_disk *disk, uint64_t lba,
                       uint32_t count, uint8_t **base) {
    if (disk == NULL || count == 0U || lba > DISK_BLOCKS ||
        count > DISK_BLOCKS - lba) return false;
    if (lba + count <= EDGE_BLOCKS) {
        *base = disk->first + lba * 512ULL;
        return true;
    }
    if (lba >= FORMAT_BASE && lba + count <= FORMAT_BASE + FORMAT_BLOCKS) {
        *base = disk->format_area + (lba - FORMAT_BASE) * 512ULL;
        return true;
    }
    if (disk->nxfs_base != 0U && lba >= disk->nxfs_base &&
        lba + count <= disk->nxfs_base + 128U) {
        *base = disk->nxfs_area + (lba - disk->nxfs_base) * 512ULL;
        return true;
    }
    if (lba >= DISK_BLOCKS - EDGE_BLOCKS) {
        *base = disk->last + (lba - (DISK_BLOCKS - EDGE_BLOCKS)) * 512ULL;
        return true;
    }
    return false;
}

static bool mock_read(void *context, uint64_t lba, uint32_t count, void *buffer) {
    struct mock_disk *disk = context;
    uint8_t *base = NULL;
    if (buffer == NULL || !mock_range(disk, lba, count, &base)) return false;
    memcpy(buffer, base, (size_t)count * 512U);
    return true;
}
static bool mock_write(void *context, uint64_t lba, uint32_t count,
                       const void *buffer) {
    struct mock_disk *disk = context;
    uint8_t *base = NULL;
    if (buffer == NULL || !mock_range(disk, lba, count, &base)) return false;
    memcpy(base, buffer, (size_t)count * 512U);
    return true;
}
static bool mock_flush(void *context) {
    struct mock_disk *disk = context;
    if (disk == NULL) return false;
    ++disk->flushes;
    return true;
}
static struct nexus_block_device make_device(const char *name,
        const char *id, struct mock_disk *disk) {
    struct nexus_block_device d = {0};
    (void)snprintf(d.name, sizeof(d.name), "%s", name);
    (void)snprintf(d.stable_id, sizeof(d.stable_id), "%s", id);
    (void)snprintf(d.model, sizeof(d.model), "Nexus disposable test disk");
    d.block_count = DISK_BLOCKS;
    d.transport = NEXUS_BLOCKDEV_TRANSPORT_NVME;
    d.block_size = 512U;
    d.max_blocks_per_request = 8U;
    d.flags = NEXUS_BLOCKDEV_READ_ONLY | NEXUS_BLOCKDEV_INSTALL_CAPABLE;
    d.context = disk;
    d.read = mock_read;
    d.write = mock_write;
    d.flush = mock_flush;
    return d;
}
int main(void) {
    struct mock_disk boot = {0};
    struct mock_disk target = {0};
    blockdev_init();
    struct nexus_block_device boot_device =
        make_device("boot", "TEST-BOOT-0001", &boot);
    struct nexus_block_device target_device =
        make_device("target", "TEST-TARGET-0002", &target);
    assert(blockdev_register(&boot_device));
    assert(blockdev_register(&target_device));
    const struct nexus_block_device *boot_handle = blockdev_at(0U);
    const struct nexus_block_device *target_handle = blockdev_at(1U);
    assert(boot_handle != NULL && target_handle != NULL);

    struct nexus_installer_disk_identity identity = {0};
    (void)snprintf(identity.stable_id, sizeof(identity.stable_id), "%s",
                   target_handle->stable_id);
    (void)snprintf(identity.model, sizeof(identity.model), "%s",
                   target_handle->model);
    identity.block_count = target_handle->block_count;
    identity.block_size = target_handle->block_size;

    struct nexus_partition_plan plan;
    assert(nexus_partition_plan_create(DISK_BLOCKS, 512U,
                                       NEXUS_FIRMWARE_UEFI, &plan));
    uint8_t disk_guid[16] = {0};
    uint8_t partition_guids[NEXUS_PLAN_MAX_PARTITIONS][16] = {{0}};
    for (size_t i = 0U; i < 16U; ++i) {
        disk_guid[i] = (uint8_t)(0xA0U + i);
        for (size_t p = 0U; p < NEXUS_PLAN_MAX_PARTITIONS; ++p)
            partition_guids[p][i] = (uint8_t)(0x10U + p * 16U + i);
    }

    /* Unknown boot provenance denies all destructive writes. */
    assert(nexus_installer_write_gpt(target_handle, &identity,
        identity.stable_id, "ERASE", &plan, disk_guid,
        &partition_guids[0][0]) == NEXUS_GPT_INSTALL_UNSAFE_TARGET);
    assert(target.flushes == 0U);

    /* Emulate a bootloader handoff containing the boot partition GUID. */
    struct nexus_gpt_image_buffers boot_image;
    assert(nexus_gpt_image_build(&plan, disk_guid,
        &partition_guids[0][0], &boot_image));
    memcpy(boot.first, boot_image.protective_mbr, 512U);
    memcpy(boot.first + 512U, boot_image.primary_header, 512U);
    memcpy(boot.first + 1024U, boot_image.primary_entries,
           NEXUS_GPT_IMAGE_ENTRY_BYTES);
    memcpy(boot.last + (EDGE_BLOCKS - 33U) * 512U,
           boot_image.backup_entries, NEXUS_GPT_IMAGE_ENTRY_BYTES);
    memcpy(boot.last + (EDGE_BLOCKS - 1U) * 512U,
           boot_image.backup_header, 512U);
    const uint8_t missing_guid[16] = {0xFEU};
    assert(!blockdev_set_boot_media_from_gpt_partition_guid(missing_guid));
    /* Two valid disks carrying the same partition GUID are ambiguous. */
    memcpy(target.first, boot.first, sizeof(target.first));
    memcpy(target.last, boot.last, sizeof(target.last));
    assert(!blockdev_set_boot_media_from_gpt_partition_guid(
        &partition_guids[0][0]));
    memset(target.first, 0, sizeof(target.first));
    memset(target.last, 0, sizeof(target.last));
    assert(blockdev_set_boot_media_from_gpt_partition_guid(
        &partition_guids[0][0]));
    assert(!blockdev_set_boot_media_from_gpt_partition_guid(
        &partition_guids[0][0]));
    assert(nexus_installer_write_gpt(boot_handle, &identity,
        identity.stable_id, "ERASE", &plan, disk_guid,
        &partition_guids[0][0]) == NEXUS_GPT_INSTALL_UNSAFE_TARGET);
    assert(nexus_installer_write_gpt(target_handle, &identity,
        identity.stable_id, "WRONG", &plan, disk_guid,
        &partition_guids[0][0]) == NEXUS_GPT_INSTALL_UNSAFE_TARGET);
    assert(target.flushes == 0U);

    assert(blockdev_set_mounted(target_handle, true));
    assert(nexus_installer_write_gpt(target_handle, &identity,
        identity.stable_id, "ERASE", &plan, disk_guid,
        &partition_guids[0][0]) == NEXUS_GPT_INSTALL_UNSAFE_TARGET);
    assert(target.flushes == 0U);
    assert(blockdev_set_mounted(target_handle, false));

    assert(nexus_installer_write_gpt(target_handle, &identity,
        identity.stable_id, "ERASE", &plan, disk_guid,
        &partition_guids[0][0]) == NEXUS_GPT_INSTALL_OK);
    assert(target.flushes == 5U);

    /* The guarded adapter formats only the exact planned EFI System Partition. */
    const struct nexus_planned_partition *esp = &plan.partitions[0];
    assert(esp->kind == NEXUS_PARTITION_ESP);
    const uint64_t esp_blocks = esp->last_lba - esp->first_lba + 1U;
    assert(nexus_installer_format_fat32(target_handle, &identity,
        identity.stable_id, "ERASE", esp->first_lba, esp_blocks,
        0x12345678U) == NEXUS_FAT32_FORMAT_OK);
    assert(target.flushes == 7U);
    const size_t esp_offset = (size_t)(esp->first_lba - FORMAT_BASE) * 512U;
    assert(target.format_area[esp_offset + 510U] == 0x55U);
    assert(target.format_area[esp_offset + 511U] == 0xAAU);

    /* Format the planned Nexus System partition through the guarded NXFS path. */
    target.nxfs_base = plan.partitions[1].first_lba;
    assert(gpt_scan_device(target_handle));
    const struct nexus_gpt_partition *system_part = gpt_partition_at(1U);
    assert(system_part != NULL);
    struct nexus_gpt_partition system_copy = *system_part;
    assert(nexus_installer_format_nxfs(target_handle, &identity,
        identity.stable_id, "ERASE", &system_copy));
    assert(target.flushes == 12U);
    assert(memcmp(target.nxfs_area, "NEXUSFS1", 8U) == 0);

    /* FAT32 formatting rejects arbitrary LBAs and non-ESP GPT partitions. */
    assert(nexus_installer_format_fat32(target_handle, &identity,
        identity.stable_id, "ERASE", 0U, 1024U, 0x12345678U) ==
        NEXUS_FAT32_FORMAT_INVALID);
    assert(target.flushes == 12U);

    identity.block_count--;
    assert(nexus_installer_write_gpt(target_handle, &identity,
        target_handle->stable_id, "ERASE", &plan, disk_guid,
        &partition_guids[0][0]) == NEXUS_GPT_INSTALL_UNSAFE_TARGET);

    /* MBR provenance must also be unique across all registered disks. */
    blockdev_init();
    memset(boot.first, 0, sizeof(boot.first));
    memset(boot.last, 0, sizeof(boot.last));
    const uint32_t mbr_id = 0x78563412U;
    boot.first[440] = 0x12U;
    boot.first[441] = 0x34U;
    boot.first[442] = 0x56U;
    boot.first[443] = 0x78U;
    boot.first[510] = 0x55U;
    boot.first[511] = 0xAAU;
    boot_device = make_device("boot-mbr", "TEST-BOOT-MBR", &boot);
    target_device = make_device("target-mbr", "TEST-TARGET-MBR", &target);
    assert(blockdev_register(&boot_device));
    assert(blockdev_register(&target_device));
    memcpy(target.first, boot.first, sizeof(target.first));
    assert(!blockdev_set_boot_media_from_mbr_disk_id(mbr_id));
    memset(target.first, 0, sizeof(target.first));
    assert(blockdev_set_boot_media_from_mbr_disk_id(mbr_id));
    assert(!blockdev_set_boot_media_from_mbr_disk_id(mbr_id));

    /* Optical boot is safe only when no optical block device is registered. */
    blockdev_init();
    target_device = make_device("target-optical", "TEST-TARGET-OPTICAL", &target);
    assert(blockdev_register(&target_device));
    assert(blockdev_set_boot_media_external(NEXUS_BOOT_SOURCE_OPTICAL));
    const struct nexus_block_device *optical_target = blockdev_at(0U);
    assert(optical_target != NULL);
    assert(blockdev_install_session_begin(optical_target,
        optical_target->stable_id, "ERASE"));
    blockdev_install_session_end();

    blockdev_init();
    target_device = make_device("target-optical", "TEST-TARGET-OPTICAL", &target);
    struct nexus_block_device optical_device =
        make_device("optical", "TEST-OPTICAL", &boot);
    optical_device.transport = NEXUS_BLOCKDEV_TRANSPORT_OPTICAL;
    assert(blockdev_register(&target_device));
    assert(blockdev_register(&optical_device));
    assert(!blockdev_set_boot_media_external(NEXUS_BOOT_SOURCE_OPTICAL));

    puts("NEXUS KERNEL INSTALLER STORAGE ADAPTER TESTS PASSED");
    return 0;
}
