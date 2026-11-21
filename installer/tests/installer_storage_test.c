#include "installer_storage.h"
#include "blockdev.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISK_BLOCKS (16ULL * 1024ULL * 1024ULL * 1024ULL / 512ULL)
#define EDGE_BLOCKS 64U

struct mock_disk {
    uint8_t first[EDGE_BLOCKS * 512U];
    uint8_t last[EDGE_BLOCKS * 512U];
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

    assert(blockdev_set_boot_media(boot_handle));
    assert(blockdev_set_boot_media_detection_complete());
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

    identity.block_count--;
    assert(nexus_installer_write_gpt(target_handle, &identity,
        target_handle->stable_id, "ERASE", &plan, disk_guid,
        &partition_guids[0][0]) == NEXUS_GPT_INSTALL_UNSAFE_TARGET);
    puts("NEXUS KERNEL INSTALLER STORAGE ADAPTER TESTS PASSED");
    return 0;
}
