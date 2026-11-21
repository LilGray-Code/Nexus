#ifndef NEXUS_INSTALLER_STORAGE_H
#define NEXUS_INSTALLER_STORAGE_H

#include "blockdev.h"
#include "gpt_install.h"
#include "fat32_format.h"
#include "fat32_file.h"
#include "nxfs.h"

struct nexus_installer_disk_identity {
    char stable_id[NEXUS_BLOCKDEV_ID_MAX];
    char model[NEXUS_BLOCKDEV_MODEL_MAX];
    uint64_t block_count;
    uint32_t block_size;
};

/* Kernel adapters revalidate device identity and obtain kernel-owned
 * destructive-write sessions. FAT32 formatting is restricted to an exact
 * GPT EFI System Partition; GPT writing changes metadata only. Neither
 * operation deploys system files or installs a bootloader. */
bool nexus_installer_write_nxfs_file(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *captured_identity,
    const char *typed_stable_id,const char *typed_erase_phrase,
    const struct nexus_gpt_partition *partition,
    const char *name,const void *data,uint32_t length);
bool nexus_installer_format_nxfs(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *captured_identity,
    const char *typed_stable_id,
    const char *typed_erase_phrase,
    const struct nexus_gpt_partition *partition);

enum nexus_fat32_file_result nexus_installer_write_efi_file(
 const struct nexus_block_device*,const struct nexus_installer_disk_identity*,
 const char*,const char*,uint64_t,uint64_t,const char*,const void*,uint32_t);
enum nexus_fat32_format_result nexus_installer_format_fat32(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *captured_identity,
    const char *typed_stable_id,
    const char *typed_erase_phrase,
    uint64_t partition_first_lba,
    uint64_t partition_block_count,
    uint32_t volume_id);

enum nexus_gpt_install_result nexus_installer_write_gpt(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *captured_identity,
    const char *typed_stable_id,
    const char *typed_erase_phrase,
    const struct nexus_partition_plan *plan,
    const uint8_t disk_guid[16],
    const uint8_t *partition_guids);

#endif
