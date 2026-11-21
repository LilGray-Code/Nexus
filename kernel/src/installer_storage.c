#include "installer_storage.h"

#include "blockdev.h"
#include "fat32_format.h"

static bool installer_target_read(void *context, uint64_t lba,
                                  uint32_t blocks, void *buffer) {
    return blockdev_read((const struct nexus_block_device *)context,
                         lba, blocks, buffer);
}

static bool installer_target_write(void *context, uint64_t lba,
                                   uint32_t blocks, const void *buffer) {
    return blockdev_install_write((const struct nexus_block_device *)context,
                                  lba, blocks, buffer);
}

static bool installer_target_flush(void *context) {
    return blockdev_install_flush((const struct nexus_block_device *)context);
}

enum nexus_fat32_format_result nexus_installer_format_fat32(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *captured_identity,
    const char *typed_stable_id,
    const char *typed_erase_phrase,
    uint64_t partition_first_lba,
    uint64_t partition_block_count,
    uint32_t volume_id) {
    if (device == NULL || captured_identity == NULL ||
        partition_block_count == 0U)
        return NEXUS_FAT32_FORMAT_INVALID;
    if (!blockdev_matches_identity(device, captured_identity->stable_id,
            captured_identity->model, captured_identity->block_count,
            captured_identity->block_size))
        return NEXUS_FAT32_FORMAT_INVALID;
    if (!blockdev_install_session_begin(device, typed_stable_id,
                                        typed_erase_phrase))
        return NEXUS_FAT32_FORMAT_INVALID;
    const struct nexus_fat32_format_target target = {
        .block_count = device->block_count,
        .block_size = device->block_size,
        .max_blocks_per_request = device->max_blocks_per_request,
        .context = (void *)device,
        .write = installer_target_write,
        .read = installer_target_read,
        .flush = installer_target_flush
    };
    const enum nexus_fat32_format_result result = nexus_fat32_format(
        &target, partition_first_lba, partition_block_count, volume_id);
    blockdev_install_session_end();
    return result;
}

enum nexus_gpt_install_result nexus_installer_write_gpt(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *captured_identity,
    const char *typed_stable_id,
    const char *typed_erase_phrase,
    const struct nexus_partition_plan *plan,
    const uint8_t disk_guid[16],
    const uint8_t *partition_guids) {
    if (device == NULL || captured_identity == NULL || plan == NULL ||
        disk_guid == NULL || partition_guids == NULL)
        return NEXUS_GPT_INSTALL_INVALID;

    /* Revalidate the exact device selection immediately before authorization. */
    if (!blockdev_matches_identity(device, captured_identity->stable_id,
            captured_identity->model, captured_identity->block_count,
            captured_identity->block_size))
        return NEXUS_GPT_INSTALL_UNSAFE_TARGET;

    if (!blockdev_install_session_begin(device, typed_stable_id,
                                        typed_erase_phrase))
        return NEXUS_GPT_INSTALL_UNSAFE_TARGET;

    /* Session creation is the kernel-owned proof that the target is not the
     * boot medium and is not mounted. Do not accept UI-supplied safety flags. */
    const struct nexus_gpt_install_target target = {
        .block_count = device->block_count,
        .block_size = device->block_size,
        .max_blocks_per_request = device->max_blocks_per_request,
        .context = (void *)device,
        .read = installer_target_read,
        .write = installer_target_write,
        .flush = installer_target_flush,
        .identity_revalidated = true,
        .not_live_media = true,
        .not_mounted = true
    };
    const enum nexus_gpt_install_result result = nexus_gpt_install_apply(
        &target, plan, disk_guid, partition_guids, true, true);
    blockdev_install_session_end();
    return result;
}
