#include "installer_storage.h"

#include "blockdev.h"
#include "fat32_format.h"
#include "fat32_file.h"
#include "gpt.h"
#include "nxfs.h"
#include "boot_payload.h"
#include "bios_install.h"

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
    static const uint8_t recovery_guid[16]={
        0x4EU,0x58U,0x52U,0x43U,0x01U,0x00U,0x58U,0x4EU,
        0x9AU,0x11U,0x4EU,0x58U,0x55U,0x53U,0x52U,0x43U
    };
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
        const struct nexus_gpt_partition *next =
            i+1U<gpt_partition_count()?gpt_partition_at(i+1U):NULL;
        if(!equal||next==NULL)return false;
        for(size_t b=0U;b<16U;++b)
            if(next->type_guid[b]!=recovery_guid[b])return false;
        return true;
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
 const enum nexus_fat32_file_result result=nexus_fat32_write_file(
  &target,first,blocks,name,data,length);
 blockdev_install_session_end();return result;
}


static bool installer_is_exact_recovery_partition(
    const struct nexus_block_device *device,
    const struct nexus_gpt_partition *requested) {
    static const uint8_t recovery_guid[16]={
        0x4EU,0x58U,0x52U,0x43U,0x01U,0x00U,0x58U,0x4EU,
        0x9AU,0x11U,0x4EU,0x58U,0x55U,0x53U,0x52U,0x43U
    };
    if(device==NULL||requested==NULL||
       requested->last_lba<requested->first_lba||
       requested->last_lba-requested->first_lba<1U||
       !gpt_scan_device(device))return false;
    for(size_t i=0U;i<gpt_partition_count();++i){
        const struct nexus_gpt_partition *part=gpt_partition_at(i);
        if(part==NULL||part->first_lba!=requested->first_lba||
           part->last_lba!=requested->last_lba)continue;
        for(size_t b=0U;b<16U;++b)
            if(part->type_guid[b]!=recovery_guid[b]||
               part->unique_guid[b]!=requested->unique_guid[b])return false;
        return true;
    }
    return false;
}

static bool journal_target_read(void *context,uint64_t lba,uint32_t blocks,
                                void *buffer) {
    return installer_target_read(context,lba,blocks,buffer);
}
static bool journal_target_write(void *context,uint64_t lba,uint32_t blocks,
                                 const void *buffer) {
    return installer_target_write(context,lba,blocks,buffer);
}
static bool journal_target_flush(void *context) {
    return installer_target_flush(context);
}

enum nexus_installer_journal_result nexus_installer_load_recovery_record(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *identity,
    const struct nexus_gpt_partition *recovery_partition,
    struct nexus_installer_journal_record *record) {
    if(device==NULL||identity==NULL||record==NULL||
       !blockdev_matches_identity(device,identity->stable_id,identity->model,
          identity->block_count,identity->block_size)||
       !installer_is_exact_recovery_partition(device,recovery_partition))
        return NEXUS_JOURNAL_INVALID;
    const struct nexus_installer_journal_device target={
        .block_count=device->block_count,.block_size=device->block_size,
        .max_blocks_per_request=device->max_blocks_per_request,
        .context=(void*)device,.read=journal_target_read,
        .write=journal_target_write,.flush=journal_target_flush
    };
    const enum nexus_installer_journal_result result=
        nexus_installer_journal_load(&target,recovery_partition->first_lba,
                                     record);
    if(result!=NEXUS_JOURNAL_OK)return result;
    const uint8_t *disk_guid=gpt_scanned_disk_guid();
    if(disk_guid==NULL||record->disk_block_count!=identity->block_count||
       record->block_size!=identity->block_size)return NEXUS_JOURNAL_CORRUPT;
    for(size_t i=0U;i<16U;++i)
        if(record->disk_guid[i]!=disk_guid[i])return NEXUS_JOURNAL_CORRUPT;
    for(size_t i=0U;i<NEXUS_BLOCKDEV_ID_MAX;++i)
        if(record->stable_id[i]!=identity->stable_id[i])
            return NEXUS_JOURNAL_CORRUPT;
    for(size_t i=0U;i<NEXUS_BLOCKDEV_MODEL_MAX;++i)
        if(record->model[i]!=identity->model[i])
            return NEXUS_JOURNAL_CORRUPT;
    return NEXUS_JOURNAL_OK;
}

enum nexus_installer_journal_result nexus_installer_store_recovery_record(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *identity,
    const char *typed_id,const char *typed_erase,
    const struct nexus_gpt_partition *recovery_partition,
    const struct nexus_installer_journal_record *record) {
    if(device==NULL||identity==NULL||record==NULL||
       !blockdev_matches_identity(device,identity->stable_id,identity->model,
          identity->block_count,identity->block_size)||
       !installer_is_exact_recovery_partition(device,recovery_partition)||
       record->disk_block_count!=identity->block_count||
       record->block_size!=identity->block_size)
        return NEXUS_JOURNAL_INVALID;
    const uint8_t *disk_guid=gpt_scanned_disk_guid();
    if(disk_guid==NULL)return NEXUS_JOURNAL_INVALID;
    for(size_t i=0U;i<16U;++i)
        if(record->disk_guid[i]!=disk_guid[i])return NEXUS_JOURNAL_INVALID;
    for(size_t i=0U;i<NEXUS_BLOCKDEV_ID_MAX;++i)
        if(record->stable_id[i]!=identity->stable_id[i])
            return NEXUS_JOURNAL_INVALID;
    for(size_t i=0U;i<NEXUS_BLOCKDEV_MODEL_MAX;++i)
        if(record->model[i]!=identity->model[i])
            return NEXUS_JOURNAL_INVALID;
    if(!blockdev_install_session_begin(device,typed_id,typed_erase))
        return NEXUS_JOURNAL_INVALID;
    const struct nexus_installer_journal_device target={
        .block_count=device->block_count,.block_size=device->block_size,
        .max_blocks_per_request=device->max_blocks_per_request,
        .context=(void*)device,.read=journal_target_read,
        .write=journal_target_write,.flush=journal_target_flush
    };
    const enum nexus_installer_journal_result result=
        nexus_installer_journal_commit(&target,recovery_partition->first_lba,
                                       record);
    blockdev_install_session_end();
    return result;
}

static bool installer_is_exact_bios_partition(
    const struct nexus_block_device *device,
    const struct nexus_gpt_partition *requested) {
    static const uint8_t bios_guid[16]={
        'H','a','h','!','I','d','o','n','t','N','e','e','d','E','F','I'
    };
    if(device==NULL||requested==NULL||requested->last_lba<requested->first_lba||
       !gpt_scan_device(device))return false;
    for(size_t i=0U;i<gpt_partition_count();++i){
        const struct nexus_gpt_partition *part=gpt_partition_at(i);
        if(part==NULL||part->first_lba!=requested->first_lba||
           part->last_lba!=requested->last_lba)continue;
        for(size_t b=0U;b<16U;++b)
            if(part->type_guid[b]!=bios_guid[b]||
               part->unique_guid[b]!=requested->unique_guid[b])return false;
        return true;
    }
    return false;
}

enum nexus_installer_system_payload_result nexus_installer_deploy_system_payload(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *identity,
    const char *typed_id,const char *typed_erase,
    const struct nexus_gpt_partition *system_partition) {
    const struct nexus_boot_payloads *payloads=nexus_boot_payloads_get();
    if(payloads==NULL||payloads->kernel_elf.data==NULL||
       payloads->kernel_elf.size==0U||
       payloads->kernel_elf.size>NEXUS_NXFS_MAX_FILE_BYTES)
        return NEXUS_SYSTEM_PAYLOAD_INVALID_PAYLOAD;
    if(!installer_is_exact_nxfs_partition(device,system_partition))
        return NEXUS_SYSTEM_PAYLOAD_INVALID_PARTITION;
    static const char manifest[]=
        "NEXUS SYSTEM IMAGE 1\n"
        "ARCH=x86_64\n"
        "KERNEL=KERNEL.ELF\n"
        "KERNEL_FORMAT=ELF64\n"
        "DESKTOP=FRAMEBUFFER\n"
        "FILESYSTEM=NXFS\n"
        "BOOT=UEFI+LEGACY_BIOS\n"
        "RELEASE_STATUS=DEVELOPMENT_PROTOTYPE\n";
    static const char readme[]=
        "Nexus OS\n"
        "This NXFS system volume contains the installed Nexus kernel and system manifest.\n"
        "The desktop, terminal, file browser, settings, and basic HTTP text viewer are currently integrated into the kernel.\n"
        "This is an engineering prototype, not a finished general-purpose desktop release.\n";
    if(!nexus_installer_write_nxfs_file(device,identity,typed_id,typed_erase,
        system_partition,"KERNEL.ELF",payloads->kernel_elf.data,
        payloads->kernel_elf.size))return NEXUS_SYSTEM_PAYLOAD_KERNEL_WRITE_FAILED;
    if(!nexus_installer_write_nxfs_file(device,identity,typed_id,typed_erase,
        system_partition,"SYSTEM.MAN",manifest,(uint32_t)(sizeof(manifest)-1U)))
        return NEXUS_SYSTEM_PAYLOAD_MANIFEST_WRITE_FAILED;
    if(!nexus_installer_write_nxfs_file(device,identity,typed_id,typed_erase,
        system_partition,"README.TXT",readme,(uint32_t)(sizeof(readme)-1U)))
        return NEXUS_SYSTEM_PAYLOAD_README_WRITE_FAILED;
    return NEXUS_SYSTEM_PAYLOAD_OK;
}

bool nexus_installer_install_bios_bootloader(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *identity,
    const char *typed_id,const char *typed_erase,
    const struct nexus_gpt_partition *bios_partition) {
    const struct nexus_boot_payloads *p=nexus_boot_payloads_get();
    if(!device||!identity||!bios_partition||!p||
       !blockdev_matches_identity(device,identity->stable_id,identity->model,
          identity->block_count,identity->block_size)||
       !installer_is_exact_bios_partition(device,bios_partition))return false;
    if(!blockdev_install_session_begin(device,typed_id,typed_erase))return false;
    const struct nexus_bios_install_target target={
      .block_count=device->block_count,.block_size=device->block_size,
      .max_blocks_per_request=device->max_blocks_per_request,
      .context=(void*)device,.read=installer_target_read,
      .write=installer_target_write,.flush=installer_target_flush
    };
    const bool ok=nexus_bios_install_apply(&target,
      bios_partition->first_lba,
      bios_partition->last_lba-bios_partition->first_lba+1U,
      p->bios_hdd_image.data,p->bios_hdd_image.size);
    blockdev_install_session_end();
    return ok;
}

bool nexus_installer_deploy_boot_payloads(
 const struct nexus_block_device *device,
 const struct nexus_installer_disk_identity *identity,
 const char *typed_id,const char *typed_erase,
 uint64_t esp_first,uint64_t esp_blocks) {
 const struct nexus_boot_payloads *p=nexus_boot_payloads_get();
 if(!p||!device||!identity)return false;
 /* Each file write revalidates the selected ESP and disk identity. The HDD
  * image is staged for guarded BIOS installation; limine-bios.sys remains
  * on the ESP because Limine's BIOS stage requires it at boot. */
 if(nexus_installer_write_efi_file(device,identity,typed_id,typed_erase,
    esp_first,esp_blocks,"EFI/BOOT/BOOTX64.EFI",
    p->uefi_loader.data,p->uefi_loader.size)!=NEXUS_FAT32_FILE_OK)return false;
 if(nexus_installer_write_efi_file(device,identity,typed_id,typed_erase,
    esp_first,esp_blocks,"NEXUS/KERNEL.ELF",
    p->kernel_elf.data,p->kernel_elf.size)!=NEXUS_FAT32_FILE_OK)return false;
 if(nexus_installer_write_efi_file(device,identity,typed_id,typed_erase,
    esp_first,esp_blocks,"boot/limine/limine-bios-hdd.bin",
    p->bios_hdd_image.data,p->bios_hdd_image.size)!=NEXUS_FAT32_FILE_OK)return false;
 if(nexus_installer_write_efi_file(device,identity,typed_id,typed_erase,
    esp_first,esp_blocks,"boot/limine/limine-bios.sys",
    p->bios_sys.data,p->bios_sys.size)!=NEXUS_FAT32_FILE_OK)return false;
 static const char config[]=
  "timeout: 3\n"
  "default_entry: 1\n"
  "interface_branding: NEXUS OS\n"
  "interface_branding_colour: 18D2EB\n"
  "\n/Nexus OS\n"
  "    protocol: limine\n"
  "    path: boot():/NEXUS/KERNEL.ELF\n"
  "    module_path: boot():/EFI/BOOT/BOOTX64.EFI\n"
  "    module_path: boot():/boot/limine/limine-bios-hdd.bin\n"
  "    module_path: boot():/boot/limine/limine-bios.sys\n";
 if(nexus_installer_write_efi_file(device,identity,typed_id,typed_erase,
    esp_first,esp_blocks,"EFI/BOOT/limine.conf",
    config,(uint32_t)(sizeof(config)-1U))!=NEXUS_FAT32_FILE_OK)return false;
 if(nexus_installer_write_efi_file(device,identity,typed_id,typed_erase,
    esp_first,esp_blocks,"boot/limine/limine.conf",
    config,(uint32_t)(sizeof(config)-1U))!=NEXUS_FAT32_FILE_OK)return false;
 return true;
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
