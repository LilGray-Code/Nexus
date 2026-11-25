#include "installer_storage.h"
#include "installer_runtime.h"
#include "blockdev.h"
#include "timer.h"
#include "gpt_image.h"
#include <limine.h>
#include "boot_payload.h"
#include "boot_pack.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define DISK_BLOCKS (16ULL * 1024ULL * 1024ULL * 1024ULL / 512ULL)
#define EDGE_BLOCKS 64U
#define FORMAT_BASE 2048U
#define FORMAT_BLOCKS 32768U /* 16 MiB of the planned 512 MiB ESP */
#define NXFS_TEST_AREA_BLOCKS 32768U
#define BIOS_TEST_AREA_BLOCKS 4096U

struct mock_disk {
    uint8_t first[EDGE_BLOCKS * 512U];
    uint8_t last[EDGE_BLOCKS * 512U];
    uint8_t format_area[FORMAT_BLOCKS * 512U];
    uint64_t nxfs_base;
    uint64_t data_base;
    uint8_t data_area[NXFS_TEST_AREA_BLOCKS * 512U];
    uint64_t recovery_base;
    uint8_t recovery_area[2U * 512U];
    uint8_t nxfs_area[NXFS_TEST_AREA_BLOCKS * 512U];
    uint64_t bios_base;
    uint8_t bios_area[BIOS_TEST_AREA_BLOCKS * 512U];
    unsigned flushes;
};

static bool bytes_contain_text(const uint8_t *bytes, size_t length,
                                const char *text) {
    const size_t text_length = strlen(text);
    if (bytes == NULL || text == NULL || text_length == 0U ||
        text_length > length) return false;
    for (size_t i = 0U; i <= length - text_length; ++i) {
        if (memcmp(bytes + i, text, text_length) == 0) return true;
    }
    return false;
}

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
    if (disk->bios_base != 0U && lba >= disk->bios_base &&
        lba + count <= disk->bios_base + BIOS_TEST_AREA_BLOCKS) {
        *base = disk->bios_area + (lba - disk->bios_base) * 512ULL;
        return true;
    }
    if (disk->data_base != 0U && lba >= disk->data_base &&
        lba + count <= disk->data_base + NXFS_TEST_AREA_BLOCKS) {
        *base = disk->data_area + (lba - disk->data_base) * 512ULL;
        return true;
    }
    if (disk->nxfs_base != 0U && lba >= disk->nxfs_base &&
        lba + count <= disk->nxfs_base + NXFS_TEST_AREA_BLOCKS) {
        *base = disk->nxfs_area + (lba - disk->nxfs_base) * 512ULL;
        return true;
    }
    if (disk->recovery_base != 0U && lba >= disk->recovery_base &&
        lba + count <= disk->recovery_base + 2U) {
        *base = disk->recovery_area + (lba - disk->recovery_base) * 512ULL;
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

static bool load_payload_file(const char *path,uint8_t **data,uint32_t *size){
    FILE *f=fopen(path,"rb");
    if(!f){fprintf(stderr,"installer test: cannot open payload '%s'\\n",path);return false;}
    if(fseek(f,0,SEEK_END)!=0){fprintf(stderr,"installer test: cannot seek payload '%s'\\n",path);fclose(f);return false;}
    long n=ftell(f);
    fprintf(stderr,"installer test: payload '%s' is %ld bytes (maximum 2097152)\\n",path,n);
    if(n<=0||(unsigned long)n>2UL*1024UL*1024UL){fclose(f);return false;}
    if(fseek(f,0,SEEK_SET)!=0){fprintf(stderr,"installer test: cannot rewind payload '%s'\\n",path);fclose(f);return false;}
    uint8_t *p=malloc((size_t)n);
    if(!p){fprintf(stderr,"installer test: cannot allocate %ld-byte payload\\n",n);fclose(f);return false;}
    bool ok=fread(p,1U,(size_t)n,f)==(size_t)n;
    if(fclose(f)!=0)ok=false;
    if(!ok){fprintf(stderr,"installer test: short read for payload '%s'\\n",path);free(p);return false;}
    *data=p;*size=(uint32_t)n;return true;
}
static bool dump_installed_disk(const char *path,const struct mock_disk *disk){
    FILE *f=fopen(path,"wb");if(!f)return false;
    bool ok=fseek(f,(long)(DISK_BLOCKS*512ULL-1ULL),SEEK_SET)==0&&fputc(0,f)!=EOF;
    if(ok)ok=fseek(f,0,SEEK_SET)==0&&fwrite(disk->first,1U,sizeof(disk->first),f)==sizeof(disk->first);
    if(ok)ok=fseek(f,(long)(FORMAT_BASE*512ULL),SEEK_SET)==0&&fwrite(disk->format_area,1U,sizeof(disk->format_area),f)==sizeof(disk->format_area);
    if(ok&&disk->nxfs_base!=0U)ok=fseek(f,(long)(disk->nxfs_base*512ULL),SEEK_SET)==0&&fwrite(disk->nxfs_area,1U,sizeof(disk->nxfs_area),f)==sizeof(disk->nxfs_area);
    if(ok&&disk->data_base!=0U)ok=fseek(f,(long)(disk->data_base*512ULL),SEEK_SET)==0&&fwrite(disk->data_area,1U,sizeof(disk->data_area),f)==sizeof(disk->data_area);
    if(ok&&disk->bios_base!=0U)ok=fseek(f,(long)(disk->bios_base*512ULL),SEEK_SET)==0&&fwrite(disk->bios_area,1U,sizeof(disk->bios_area),f)==sizeof(disk->bios_area);
    if(ok&&disk->recovery_base!=0U)ok=fseek(f,(long)(disk->recovery_base*512ULL),SEEK_SET)==0&&fwrite(disk->recovery_area,1U,sizeof(disk->recovery_area),f)==sizeof(disk->recovery_area);
    if(ok)ok=fseek(f,(long)((DISK_BLOCKS-EDGE_BLOCKS)*512ULL),SEEK_SET)==0&&fwrite(disk->last,1U,sizeof(disk->last),f)==sizeof(disk->last);
    if(fclose(f)!=0)ok=false;
    return ok;
}

uint64_t timer_ticks(void) { return 123456U; }

static uint32_t test_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return ~crc;
}
static void test_write_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}
static void test_make_system_pack(uint8_t image[80]) {
    for (size_t i = 0U; i < 80U; ++i) image[i] = 0U;
    const uint8_t magic[8] = {'N','X','S','P','A','C','K','1'};
    for (size_t i = 0U; i < 8U; ++i) image[i] = magic[i];
    test_write_le32(image + 8U, 1U);
    test_write_le32(image + 12U, 80U);
    test_write_le32(image + 16U, 80U);
    test_write_le32(image + 20U, 1U);
    uint8_t *record = image + 32U;
    const char name[] = "BOOT.TXT";
    for (size_t i = 0U; i < sizeof(name) - 1U; ++i)
        record[i] = (uint8_t)name[i];
    test_write_le32(record + 32U, 80U);
    test_write_le32(record + 36U, 0U);
    test_write_le32(record + 40U, 0U);
    test_write_le32(record + 44U, 0U);
    test_write_le32(image + 24U, test_crc32(record, 48U));
    test_write_le32(image + 28U, 0U);
}

int main(int argc,char **argv) {
    uint8_t *external_kernel=NULL,*external_uefi=NULL,*external_bios_hdd=NULL,*external_bios_sys=NULL;
    uint32_t external_kernel_size=0U,external_uefi_size=0U,external_bios_hdd_size=0U,external_bios_sys_size=0U;
    if(argc!=1&&argc!=6)return 2;
    if(argc==6){
        assert(load_payload_file(argv[1],&external_kernel,&external_kernel_size));
        assert(load_payload_file(argv[2],&external_uefi,&external_uefi_size));
        assert(load_payload_file(argv[3],&external_bios_hdd,&external_bios_hdd_size));
        assert(load_payload_file(argv[4],&external_bios_sys,&external_bios_sys_size));
    }
    static struct mock_disk boot = {0};
    static struct mock_disk target = {0};
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
                                       NEXUS_FIRMWARE_DUAL, &plan));
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

    /* Resolve the actual boot files supplied by Limine and deploy them to
     * the selected ESP using only the kernel-owned guarded write adapter. */
    static uint8_t fake_kernel[64] = {0x7fU,'E','L','F',2U,1U};
    static uint8_t fake_uefi[512] = {'M','Z'};
    static uint8_t fake_bios_hdd[1024] = {0};
    static uint8_t fake_bios_sys[512] = {0};
    static uint8_t fake_system_pack[80] = {0};
    test_make_system_pack(fake_system_pack);
    fake_bios_hdd[510]=0x55U;fake_bios_hdd[511]=0xAAU;
    fake_uefi[0x3c]=0x40U;fake_uefi[0x40]='P';fake_uefi[0x41]='E';
    uint8_t *kernel_payload=argc==6?external_kernel:fake_kernel;
    uint8_t *uefi_payload=argc==6?external_uefi:fake_uefi;
    uint8_t *bios_hdd_payload=argc==6?external_bios_hdd:fake_bios_hdd;
    uint8_t *bios_sys_payload=argc==6?external_bios_sys:fake_bios_sys;
    uint32_t kernel_payload_size=argc==6?external_kernel_size:(uint32_t)sizeof(fake_kernel);
    uint32_t uefi_payload_size=argc==6?external_uefi_size:(uint32_t)sizeof(fake_uefi);
    uint32_t bios_hdd_payload_size=argc==6?external_bios_hdd_size:(uint32_t)sizeof(fake_bios_hdd);
    uint32_t bios_sys_payload_size=argc==6?external_bios_sys_size:(uint32_t)sizeof(fake_bios_sys);
    struct limine_file kernel_file={.address=kernel_payload,.size=kernel_payload_size};
    struct limine_file uefi_file={.address=uefi_payload,.size=uefi_payload_size,
        .path="boot():/EFI/BOOT/BOOTX64.EFI"};
    struct limine_file bios_hdd_file={.address=bios_hdd_payload,.size=bios_hdd_payload_size,
        .path="boot():/boot/limine/limine-bios-hdd.bin"};
    struct limine_file bios_sys_file={.address=bios_sys_payload,.size=bios_sys_payload_size,
        .path="boot():/boot/limine/limine-bios.sys"};
    struct limine_file pack_file={.address=fake_system_pack,.size=sizeof(fake_system_pack),
        .path="boot():/NEXUS/SYSTEM.NXP"};
    struct limine_file *module_files[4]={&uefi_file,&bios_hdd_file,&bios_sys_file,&pack_file};
    struct limine_module_response module_response={.module_count=4U,.modules=module_files};
    struct limine_executable_file_response executable_response={.executable_file=&kernel_file};
    assert(nexus_boot_payloads_init(&module_response,&executable_response));
    assert(nexus_boot_pack_init(fake_system_pack,sizeof(fake_system_pack)));
    assert(nexus_installer_deploy_boot_payloads(target_handle,&identity,
        identity.stable_id,"ERASE",esp->first_lba,esp_blocks));
    assert(target.flushes == 30U);
    /* The target disk must boot straight into the installed desktop, never
     * back into the temporary USB/ISO installer environment. */
    assert(bytes_contain_text(target.format_area, sizeof(target.format_area),
                              "cmdline: nexus.mode=desktop"));
    assert(!bytes_contain_text(target.format_area, sizeof(target.format_area),
                               "cmdline: nexus.mode=installer"));

    /* Install BIOS stage-1 into LBA 0 and stage-2 into the authenticated
     * GPT BIOS Boot partition, using the same kernel-owned write gate. */
    target.bios_base=plan.partitions[1].first_lba;
    assert(plan.partitions[1].kind==NEXUS_PARTITION_BIOS_BOOT);
    assert(gpt_scan_device(target_handle));
    const struct nexus_gpt_partition *bios_part=gpt_partition_at(1U);
    assert(bios_part!=NULL);
    struct nexus_gpt_partition bios_copy=*bios_part;
    assert(nexus_installer_install_bios_bootloader(target_handle,&identity,
        identity.stable_id,"ERASE",&bios_copy));
    assert(target.flushes==32U);
    assert(memcmp(target.bios_area,bios_hdd_payload+512U,
        bios_hdd_payload_size-512U)==0);
    assert(target.first[510U]==0x55U&&target.first[511U]==0xAAU);
    assert(memcmp(target.first+440U,boot_image.protective_mbr+440U,70U)==0);

    /* Persist installer recovery records in the authenticated GPT recovery
     * partition through the same guarded block-device write gate. */
    assert(plan.partitions[3].kind==NEXUS_PARTITION_RECOVERY);
    target.recovery_base=plan.partitions[3].first_lba;
    assert(gpt_scan_device(target_handle));
    const struct nexus_gpt_partition *recovery_part=gpt_partition_at(3U);
    assert(recovery_part!=NULL);
    struct nexus_gpt_partition recovery_copy=*recovery_part;
    struct nexus_installer_journal_record journal={0},journal_readback;
    journal.sequence=1U;journal.phase=NEXUS_JOURNAL_PREPARED;
    journal.disk_block_count=target_handle->block_count;
    journal.block_size=target_handle->block_size;
    journal.firmware_mode=NEXUS_FIRMWARE_DUAL;
    (void)snprintf(journal.stable_id,sizeof(journal.stable_id),"%s",
                   target_handle->stable_id);
    (void)snprintf(journal.model,sizeof(journal.model),"%s",
                   target_handle->model);
    memcpy(journal.disk_guid,disk_guid,sizeof(journal.disk_guid));
    assert(nexus_installer_store_recovery_record(target_handle,&identity,
        identity.stable_id,"ERASE",&recovery_copy,&journal)==NEXUS_JOURNAL_OK);
    assert(nexus_installer_load_recovery_record(target_handle,&identity,
        &recovery_copy,&journal_readback)==NEXUS_JOURNAL_OK);
    assert(journal_readback.sequence==1U&&
           journal_readback.phase==NEXUS_JOURNAL_PREPARED);
    journal.sequence=2U;journal.phase=NEXUS_JOURNAL_GPT_WRITTEN;
    journal.completed_steps=1U;
    assert(nexus_installer_store_recovery_record(target_handle,&identity,
        identity.stable_id,"ERASE",&recovery_copy,&journal)==NEXUS_JOURNAL_OK);
    assert(nexus_installer_load_recovery_record(target_handle,&identity,
        &recovery_copy,&journal_readback)==NEXUS_JOURNAL_OK);
    assert(journal_readback.sequence==2U&&
           journal_readback.phase==NEXUS_JOURNAL_GPT_WRITTEN);
    assert(target.flushes==34U);
    journal.disk_guid[0]^=0xFFU;
    assert(nexus_installer_store_recovery_record(target_handle,&identity,
        identity.stable_id,"ERASE",&recovery_copy,&journal)==NEXUS_JOURNAL_INVALID);
    journal.disk_guid[0]^=0xFFU;

    const uint32_t bps=512U;
    const uint32_t reserved=(uint32_t)(target.format_area[esp_offset+14U] |
        ((uint32_t)target.format_area[esp_offset+15U]<<8));
    const uint32_t fatsz=(uint32_t)target.format_area[esp_offset+36U] |
        ((uint32_t)target.format_area[esp_offset+37U]<<8) |
        ((uint32_t)target.format_area[esp_offset+38U]<<16) |
        ((uint32_t)target.format_area[esp_offset+39U]<<24);
    const uint32_t spc=target.format_area[esp_offset+13U];
    const size_t root_off=esp_offset+(size_t)(reserved+2U*fatsz)*bps;
    assert(memcmp(target.format_area+root_off,"NEXUS OS   ",11U)==0);
    assert(target.format_area[root_off+11U]==0x08U);
    uint32_t efi_cluster=0U;
    for(size_t slot=0U;slot<16U;slot++){
        const size_t entry_off=root_off+slot*32U;
        if(memcmp(target.format_area+entry_off,"EFI        ",11U)==0 &&
           target.format_area[entry_off+11U]==0x10U){
            efi_cluster=(uint32_t)target.format_area[entry_off+26U] |
                ((uint32_t)target.format_area[entry_off+27U]<<8) |
                ((uint32_t)target.format_area[entry_off+20U]<<16) |
                ((uint32_t)target.format_area[entry_off+21U]<<24);
            break;
        }
    }
    assert(efi_cluster>=3U);
    const size_t efi_off=esp_offset+(size_t)(reserved+2U*fatsz+
        (efi_cluster-2U)*spc)*bps;
    assert(memcmp(target.format_area+efi_off+64U,"BOOT       ",11U)==0);
    assert(target.format_area[efi_off+32U+20U]==0U &&
           target.format_area[efi_off+32U+26U]==0U);
    const uint32_t boot_cluster=(uint32_t)target.format_area[efi_off+64U+26U] |
        ((uint32_t)target.format_area[efi_off+64U+27U]<<8) |
        ((uint32_t)target.format_area[efi_off+64U+20U]<<16) |
        ((uint32_t)target.format_area[efi_off+64U+21U]<<24);
    assert(boot_cluster>=3U);
    const size_t boot_off=esp_offset+(size_t)(reserved+2U*fatsz+
        (boot_cluster-2U)*spc)*bps;
    assert(target.format_area[boot_off+32U+26U]==(uint8_t)efi_cluster);
    assert(target.format_area[boot_off+64U]=='B' &&
           target.format_area[boot_off+65U]=='O');
    assert(target.format_area[boot_off+96U]==0x41U);
    assert(memcmp(target.format_area+boot_off+128U,"LIMINE~1CNF",11U)==0);
    const uint32_t config_cluster=(uint32_t)target.format_area[boot_off+128U+26U] |
        ((uint32_t)target.format_area[boot_off+128U+27U]<<8) |
        ((uint32_t)target.format_area[boot_off+128U+20U]<<16) |
        ((uint32_t)target.format_area[boot_off+128U+21U]<<24);
    assert(config_cluster>=3U);
    const size_t config_off=esp_offset+(size_t)(reserved+2U*fatsz+
        (config_cluster-2U)*spc)*bps;
    assert(memcmp(target.format_area+config_off,"timeout: 3\n",11U)==0);

    /* The BIOS stage-2 filename requires a two-entry VFAT long name. */
    uint32_t root_boot_cluster=0U;
    for(size_t slot=0U;slot<16U;slot++){
        const size_t entry_off=root_off+slot*32U;
        if(memcmp(target.format_area+entry_off,"BOOT       ",11U)==0){
            root_boot_cluster=(uint32_t)target.format_area[entry_off+26U] |
                ((uint32_t)target.format_area[entry_off+27U]<<8) |
                ((uint32_t)target.format_area[entry_off+20U]<<16) |
                ((uint32_t)target.format_area[entry_off+21U]<<24);
            break;
        }
    }
    assert(root_boot_cluster>=3U);
    const size_t root_boot_off=esp_offset+(size_t)(reserved+2U*fatsz+
        (root_boot_cluster-2U)*spc)*bps;
    assert(memcmp(target.format_area+root_boot_off+64U,"LIMINE     ",11U)==0);
    const uint32_t limine_dir_cluster=(uint32_t)target.format_area[root_boot_off+64U+26U] |
        ((uint32_t)target.format_area[root_boot_off+64U+27U]<<8) |
        ((uint32_t)target.format_area[root_boot_off+64U+20U]<<16) |
        ((uint32_t)target.format_area[root_boot_off+64U+21U]<<24);
    assert(limine_dir_cluster>=3U);
    const size_t limine_dir_off=esp_offset+(size_t)(reserved+2U*fatsz+
        (limine_dir_cluster-2U)*spc)*bps;
    assert(target.format_area[limine_dir_off+32U+26U]==(uint8_t)root_boot_cluster);
    assert(target.format_area[limine_dir_off+64U]==0x42U);
    assert(target.format_area[limine_dir_off+96U]==0x01U);
    assert(memcmp(target.format_area+limine_dir_off+128U,"LIMINE~1BIN",11U)==0);
    assert(target.format_area[limine_dir_off+160U]==0x42U);
    assert(target.format_area[limine_dir_off+192U]==0x01U);
    assert(memcmp(target.format_area+limine_dir_off+224U,"LIMINE~1SYS",11U)==0);

    /* Format the planned Nexus System partition through the guarded NXFS path. */
    target.nxfs_base = plan.partitions[2].first_lba;
    assert(gpt_scan_device(target_handle));
    const struct nexus_gpt_partition *system_part = gpt_partition_at(2U);
    assert(system_part != NULL);
    struct nexus_gpt_partition system_copy = *system_part;
    assert(nexus_installer_format_nxfs(target_handle, &identity,
        identity.stable_id, "ERASE", &system_copy));
    assert(target.flushes == 39U);
    assert(memcmp(target.nxfs_area, "NEXUSFS1", 8U) == 0);

    /* Deploy the actual kernel ELF and basic system-volume documentation. */
    const enum nexus_installer_system_payload_result payload_result=
        nexus_installer_deploy_system_payload(target_handle,&identity,
            identity.stable_id,"ERASE",&system_copy);
    if(payload_result!=NEXUS_SYSTEM_PAYLOAD_OK)
        (void)fprintf(stderr,"system payload deploy result=%u kernel_size=%u\\n",
            (unsigned)payload_result,kernel_payload_size);
    assert(payload_result==NEXUS_SYSTEM_PAYLOAD_OK);
    const uint32_t kernel_chunk_count_for_flush =
        (kernel_payload_size + NEXUS_NXFS_MAX_FILE_BYTES - 1U) /
        NEXUS_NXFS_MAX_FILE_BYTES;
    assert(kernel_chunk_count_for_flush>=1U&&kernel_chunk_count_for_flush<=2U);
    assert(target.flushes==51U+4U*(kernel_chunk_count_for_flush-1U));
    assert(gpt_scan_device(target_handle));
    const struct nexus_gpt_partition *data_part=gpt_partition_at(4U);
    assert(data_part!=NULL);
    struct nexus_gpt_partition data_copy = *data_part;
    /* Deploy a large system file through a one-operation installer session. */
    static uint8_t system_payload[256U * 1024U];
    static uint8_t system_readback[4096U];
    for(size_t i=0U;i<sizeof(system_payload);++i)
        system_payload[i]=(uint8_t)((i*29U+(i>>7))&0xFFU);
    assert(nexus_installer_write_nxfs_file(target_handle,&identity,
        identity.stable_id,"ERASE",&system_copy,"KERNEL.BIN",
        system_payload,sizeof(system_payload)));
    assert(target.flushes == 55U+4U*(kernel_chunk_count_for_flush-1U));
    assert(nxfs_mount(target_handle,&system_copy));
    const struct nexus_nxfs_entry *system_entry=NULL;
    for(size_t i=0U;i<nxfs_entry_count();++i){
        const struct nexus_nxfs_entry *entry=nxfs_entry_at(i);
        if(entry!=NULL&&strcmp(entry->name,"KERNEL.BIN")==0)system_entry=entry;
    }
    assert(system_entry!=NULL&&system_entry->size==sizeof(system_payload));
    const struct nexus_nxfs_entry *kernel_parts[2]={NULL,NULL};
    const struct nexus_nxfs_entry *manifest_entry=NULL;
    for(size_t i=0U;i<nxfs_entry_count();++i){
        const struct nexus_nxfs_entry *entry=nxfs_entry_at(i);
        if(entry==NULL)continue;
        if(strcmp(entry->name,"KERNEL.000")==0)kernel_parts[0]=entry;
        if(strcmp(entry->name,"KERNEL.001")==0)kernel_parts[1]=entry;
        if(strcmp(entry->name,"SYSTEM.MAN")==0)manifest_entry=entry;
    }
    const uint32_t kernel_chunk_count =
        (kernel_payload_size + NEXUS_NXFS_MAX_FILE_BYTES - 1U) /
        NEXUS_NXFS_MAX_FILE_BYTES;
    assert(kernel_chunk_count>=1U&&kernel_chunk_count<=2U);
    assert(manifest_entry!=NULL&&manifest_entry->size>0U);
    for(uint32_t part=0U;part<kernel_chunk_count;++part){
        assert(kernel_parts[part]!=NULL);
        const uint32_t part_offset=part*NEXUS_NXFS_MAX_FILE_BYTES;
        const uint32_t remaining=kernel_payload_size-part_offset;
        const uint32_t expected_size=remaining<NEXUS_NXFS_MAX_FILE_BYTES
            ?remaining:NEXUS_NXFS_MAX_FILE_BYTES;
        assert(kernel_parts[part]->size==expected_size);
        for(uint32_t offset=0U;offset<expected_size;offset+=sizeof(system_readback)){
            const uint32_t amount=expected_size-offset<sizeof(system_readback)
                ?expected_size-offset:(uint32_t)sizeof(system_readback);
            assert(nxfs_read_file(kernel_parts[part],offset,amount,system_readback));
            assert(memcmp(system_readback,kernel_payload+part_offset+offset,
                          amount)==0);
        }
    }
    assert((kernel_chunk_count==2U)==(kernel_parts[1]!=NULL));
    for(uint32_t offset=0U;offset<system_entry->size;offset+=sizeof(system_readback)){
        const uint32_t amount=system_entry->size-offset<sizeof(system_readback)
            ?system_entry->size-offset:(uint32_t)sizeof(system_readback);
        assert(nxfs_read_file(system_entry,offset,amount,system_readback));
        assert(memcmp(system_readback,system_payload+offset,amount)==0);
    }
    assert(!nxfs_write_file("DENIED.TXT",system_payload,1U));
    nxfs_unmount();

    /* The data volume is independently formatted and writable while the
     * system volume remains a separate, independently mounted NXFS volume. */
    target.nxfs_base = data_part->first_lba;
    assert(nexus_installer_format_nxfs(target_handle, &identity,
        identity.stable_id, "ERASE", &data_copy));
    assert(memcmp(target.nxfs_area, "NEXUSFS1", 8U) == 0);
    static const char user_data[] = "NEXUS USER DATA VOLUME";
    assert(nexus_installer_write_nxfs_file(target_handle, &identity,
        identity.stable_id, "ERASE", &data_copy, "USER.TXT",
        user_data, (uint32_t)(sizeof(user_data) - 1U)));
    assert(nxfs_mount(target_handle, &data_copy));
    const struct nexus_nxfs_entry *user_entry = NULL;
    for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
        const struct nexus_nxfs_entry *entry = nxfs_entry_at(i);
        if (entry != NULL && strcmp(entry->name, "USER.TXT") == 0)
            user_entry = entry;
    }
    assert(user_entry != NULL &&
           user_entry->size == sizeof(user_data) - 1U);
    char user_readback[128] = {0};
    assert(nxfs_read_file(user_entry, 0U, (uint32_t)sizeof(user_data) - 1U,
                          user_readback));
    assert(memcmp(user_readback, user_data, sizeof(user_data) - 1U) == 0);
    nxfs_unmount();

    /* Simulate a cold restart by discarding all NXFS in-memory mount state.
     * Re-read the user's file from the unchanged mock target sectors. */
    assert(nxfs_mount(target_handle, &data_copy));
    user_entry = NULL;
    for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
        const struct nexus_nxfs_entry *entry = nxfs_entry_at(i);
        if (entry != NULL && strcmp(entry->name, "USER.TXT") == 0)
            user_entry = entry;
    }
    memset(user_readback, 0, sizeof(user_readback));
    assert(user_entry != NULL &&
           user_entry->size == sizeof(user_data) - 1U);
    assert(nxfs_read_file(user_entry, 0U,
        (uint32_t)sizeof(user_data) - 1U, user_readback));
    assert(memcmp(user_readback, user_data, sizeof(user_data) - 1U) == 0);
    nxfs_unmount();

    /* Replace the same file using the guarded installer data adapter, then
     * cold-remount once more and verify that the newer generation is durable. */
    static const char edited_user_data[] =
        "NEXUS USER DATA VOLUME - EDITED AFTER REBOOT";
    assert(nexus_installer_write_nxfs_file(target_handle, &identity,
        identity.stable_id, "ERASE", &data_copy, "USER.TXT",
        edited_user_data, (uint32_t)(sizeof(edited_user_data) - 1U)));
    assert(nxfs_mount(target_handle, &data_copy));
    user_entry = NULL;
    for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
        const struct nexus_nxfs_entry *entry = nxfs_entry_at(i);
        if (entry != NULL && strcmp(entry->name, "USER.TXT") == 0)
            user_entry = entry;
    }
    memset(user_readback, 0, sizeof(user_readback));
    assert(user_entry != NULL &&
           user_entry->size == sizeof(edited_user_data) - 1U);
    assert(nxfs_read_file(user_entry, 0U,
        (uint32_t)sizeof(edited_user_data) - 1U, user_readback));
    assert(memcmp(user_readback, edited_user_data,
                  sizeof(edited_user_data) - 1U) == 0);
    nxfs_unmount();

    assert(nxfs_mount(target_handle, &data_copy));
    user_entry = NULL;
    for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
        const struct nexus_nxfs_entry *entry = nxfs_entry_at(i);
        if (entry != NULL && strcmp(entry->name, "USER.TXT") == 0)
            user_entry = entry;
    }
    memset(user_readback, 0, sizeof(user_readback));
    assert(user_entry != NULL &&
           user_entry->size == sizeof(edited_user_data) - 1U);
    assert(nxfs_read_file(user_entry, 0U,
        (uint32_t)sizeof(edited_user_data) - 1U, user_readback));
    assert(memcmp(user_readback, edited_user_data,
                  sizeof(edited_user_data) - 1U) == 0);
    nxfs_unmount();

    /* FAT32 formatting rejects arbitrary LBAs and non-ESP GPT partitions
     * without issuing any additional target writes. */
    const unsigned flushes_before_invalid_format = target.flushes;
    assert(nexus_installer_format_fat32(target_handle, &identity,
        identity.stable_id, "ERASE", 0U, 1024U, 0x12345678U) ==
        NEXUS_FAT32_FORMAT_INVALID);
    assert(target.flushes == flushes_before_invalid_format);

    identity.block_count--;
    assert(nexus_installer_write_gpt(target_handle, &identity,
        target_handle->stable_id, "ERASE", &plan, disk_guid,
        &partition_guids[0][0]) == NEXUS_GPT_INSTALL_UNSAFE_TARGET);

    /* Run the complete kernel installer runtime against a fresh disposable
     * NVMe image. The image dumped below is the runtime-produced installation
     * that the CI job boots under both UEFI and legacy BIOS. */
    identity.block_count = target_handle->block_count;
    memset(&target, 0, sizeof(target));
    struct nexus_partition_plan runtime_plan;
    assert(nexus_partition_plan_create(DISK_BLOCKS, 512U,
        NEXUS_FIRMWARE_DUAL, &runtime_plan));
    for (size_t i = 0U; i < runtime_plan.partition_count; ++i) {
        const struct nexus_planned_partition *part = &runtime_plan.partitions[i];
        if (part->kind == NEXUS_PARTITION_SYSTEM)
            target.nxfs_base = part->first_lba;
        else if (part->kind == NEXUS_PARTITION_DATA)
            target.data_base = part->first_lba;
        else if (part->kind == NEXUS_PARTITION_RECOVERY)
            target.recovery_base = part->first_lba;
        else if (part->kind == NEXUS_PARTITION_BIOS_BOOT)
            target.bios_base = part->first_lba;
    }
    assert(nexus_installer_runtime_apply(target_handle, NEXUS_FIRMWARE_DUAL,
        target_handle->stable_id, "ERASE", NULL, NULL) ==
        NEXUS_INSTALL_RESULT_OK);
    assert(gpt_scan_device(target_handle));
    assert(gpt_partition_count() == runtime_plan.partition_count);
    struct nexus_gpt_partition runtime_recovery =
        *gpt_partition_at(3U);
    struct nexus_installer_journal_record runtime_journal;
    assert(nexus_installer_load_recovery_record(target_handle, &identity,
        &runtime_recovery, &runtime_journal) == NEXUS_JOURNAL_OK);
    assert(runtime_journal.phase == NEXUS_JOURNAL_COMPLETE);
    assert(runtime_journal.completed_steps == 5U);
    if(argc==6)assert(dump_installed_disk(argv[5],&target));

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
    free(external_kernel);free(external_uefi);free(external_bios_hdd);free(external_bios_sys);
    return 0;
}
