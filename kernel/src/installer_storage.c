#include "installer_storage.h"

#include "blockdev.h"
#include "fat32_format.h"
#include "fat32_file.h"
#include "gpt.h"
#include "nxfs.h"

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

static bool installer_is_exact_esp_partition(
    const struct nexus_block_device *device,uint64_t first,uint64_t count) {
    static const uint8_t esp_guid[16]={
        0x28U,0x73U,0x2AU,0xC1U,0x1FU,0xF8U,0xD2U,0x11U,
        0xBAU,0x4BU,0x00U,0xA0U,0xC9U,0x3EU,0xC9U,0x3BU
    };
    if(count==0U||first>UINT64_MAX-(count-1U)||
       first+count-1U>=device->block_count||!gpt_scan_device(device))
        return false;
    for(size_t i=0U;i<gpt_partition_count();++i){
        const struct nexus_gpt_partition *part=gpt_partition_at(i);
        if(part==NULL||part->first_lba!=first||
           part->last_lba!=first+count-1U)continue;
        bool matches=true;
        for(size_t b=0U;b<16U;++b)
            if(part->type_guid[b]!=esp_guid[b])matches=false;
        return matches;
    }
    return false;
}

static bool installer_is_exact_nxfs_partition(
    const struct nexus_block_device *device,
    const struct nexus_gpt_partition *requested) {
    if (device == NULL || requested == NULL ||
        !nxfs_partition_type_matches(requested) ||
        !gpt_scan_device(device)) return false;
    for (size_t i=0U;i<gpt_partition_count();++i) {
        const struct nexus_gpt_partition *part=gpt_partition_at(i);
        if (part == NULL || part->first_lba != requested->first_lba ||
            part->last_lba != requested->last_lba) continue;
        bool equal=true;
        for (size_t b=0U;b<16U;++b) {
            if (part->type_guid[b] != requested->type_guid[b] ||
                part->unique_guid[b] != requested->unique_guid[b])
                equal=false;
        }
        return equal;
    }
    return false;
}
bool nexus_installer_write_nxfs_file(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *captured_identity,
    const char *typed_stable_id,const char *typed_erase_phrase,
    const struct nexus_gpt_partition *partition,
    const char *name,const void *data,uint32_t length) {
    if (device == NULL || captured_identity == NULL || partition == NULL ||
        !blockdev_matches_identity(device, captured_identity->stable_id,
            captured_identity->model, captured_identity->block_count,
            captured_identity->block_size) ||
        !installer_is_exact_nxfs_partition(device, partition))
        return false;
    struct nexus_gpt_partition validated = *partition;
    return nxfs_write_file_install(device, &validated, typed_stable_id,
                                   typed_erase_phrase, name, data, length);
}

bool nexus_installer_format_nxfs(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *captured_identity,
    const char *typed_stable_id,
    const char *typed_erase_phrase,
    const struct nexus_gpt_partition *partition) {
    if (device == NULL || captured_identity == NULL || partition == NULL ||
        !blockdev_matches_identity(device, captured_identity->stable_id,
            captured_identity->model, captured_identity->block_count,
            captured_identity->block_size) ||
        !installer_is_exact_nxfs_partition(device, partition))
        return false;
    struct nexus_gpt_partition validated = *partition;
    return nxfs_format_install(device, &validated, typed_stable_id,
                               typed_erase_phrase);
}

enum nexus_fat32_file_result nexus_installer_write_efi_file(
 const struct nexus_block_device *device,
 const struct nexus_installer_disk_identity *identity,
 const char *typed_id,const char *typed_erase,
 uint64_t first,uint64_t blocks,const char *name,const void *data,uint32_t length){
 if(!device||!identity||!blocks||
    !blockdev_matches_identity(device,identity->stable_id,identity->model,
      identity->block_count,identity->block_size)||
    !installer_is_exact_esp_partition(device,first,blocks))
  return NEXUS_FAT32_FILE_INVALID;
 if(!blockdev_install_session_begin(device,typed_id,typed_erase))
  return NEXUS_FAT32_FILE_INVALID;
 const struct nexus_fat32_format_target target={
  .block_count=device->block_count,.block_size=device->block_size,
  .max_blocks_per_request=device->max_blocks_per_request,
  .context=(void*)device,.write=installer_target_write,
  .read=installer_target_read,.flush=installer_target_flush};
 const enum nexus_fat32_file_result result=nexus_fat32_write_root_file(
  &target,first,blocks,name,data,length);
 blockdev_install_session_end();return result;
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
            captured_identity->block_size) ||
        !installer_is_exact_esp_partition(device, partition_first_lba,
                                          partition_block_count))
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
