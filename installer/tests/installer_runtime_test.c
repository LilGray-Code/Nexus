#include "installer_runtime.h"
#include "installer_storage.h"
#include "installer_journal.h"
#include "gpt.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static struct nexus_block_device disk;
static bool boot_known, live_media, mounted, unique_id, write_capable;
static bool gpt_ready;
static bool fail_gpt, fail_journal, fail_esp, fail_system, fail_payload;
static bool fail_boot_payload, fail_bios;
static unsigned gpt_writes, journal_writes, esp_formats, system_formats;
static unsigned payload_writes, boot_payload_writes, bios_installs;
static enum nexus_installer_journal_result old_journal_result;
static enum nexus_installer_journal_phase old_journal_phase;
static enum nexus_installer_journal_phase journal_phases[8];
static size_t journal_phase_count;
static struct nexus_partition_plan current_plan;
static struct nexus_gpt_partition current_parts[NEXUS_PLAN_MAX_PARTITIONS];
static uint8_t current_disk_guid[16];

static const uint8_t esp_guid[16] = {
    0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,
    0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B
};
static const uint8_t bios_guid[16] = {
    'H','a','h','!','I','d','o','n','t','N','e','e','d','E','F','I'
};
static const uint8_t nxfs_guid[16] = {
    0x46,0x53,0x58,0x4E,0x01,0x00,0x58,0x4E,
    0x9A,0x11,0x4E,0x58,0x55,0x53,0x46,0x53
};
static const uint8_t recovery_guid[16] = {
    0x4E,0x58,0x52,0x43,0x01,0x00,0x58,0x4E,
    0x9A,0x11,0x4E,0x58,0x55,0x53,0x52,0x43
};

uint64_t timer_ticks(void) { return 123456U; }
bool blockdev_boot_media_detection_complete(void) { return boot_known; }
bool blockdev_is_boot_media(const struct nexus_block_device *d) {
    (void)d; return live_media;
}
bool blockdev_is_mounted(const struct nexus_block_device *d) {
    (void)d; return mounted;
}
bool blockdev_identity_is_unique(const struct nexus_block_device *d) {
    (void)d; return unique_id;
}
bool blockdev_install_write_supported(const struct nexus_block_device *d) {
    (void)d; return write_capable;
}
bool blockdev_matches_identity(const struct nexus_block_device *d,
    const char *id, const char *model, uint64_t blocks, uint32_t size) {
    return d == &disk && id != NULL && model != NULL &&
        strcmp(id, disk.stable_id) == 0 && strcmp(model, disk.model) == 0 &&
        blocks == disk.block_count && size == disk.block_size;
}

static void populate_partitions(const struct nexus_partition_plan *plan,
                                const uint8_t disk_guid[16],
                                const uint8_t *partition_guids) {
    current_plan = *plan;
    memcpy(current_disk_guid, disk_guid, 16U);
    memset(current_parts, 0, sizeof(current_parts));
    for (size_t i = 0; i < plan->partition_count; ++i) {
        const uint8_t *type = plan->partitions[i].kind == NEXUS_PARTITION_ESP
            ? esp_guid : (plan->partitions[i].kind == NEXUS_PARTITION_BIOS_BOOT
                ? bios_guid : (plan->partitions[i].kind == NEXUS_PARTITION_RECOVERY
                    ? recovery_guid : nxfs_guid));
        memcpy(current_parts[i].type_guid, type, 16U);
        memcpy(current_parts[i].unique_guid, partition_guids + i * 16U, 16U);
        current_parts[i].first_lba = plan->partitions[i].first_lba;
        current_parts[i].last_lba = plan->partitions[i].last_lba;
    }
    gpt_ready = true;
}

bool gpt_scan_device(const struct nexus_block_device *d) {
    return d == &disk && gpt_ready;
}
size_t gpt_partition_count(void) {
    return gpt_ready ? current_plan.partition_count : 0U;
}
const struct nexus_gpt_partition *gpt_partition_at(size_t index) {
    return gpt_ready && index < current_plan.partition_count
        ? &current_parts[index] : NULL;
}
const uint8_t *gpt_scanned_disk_guid(void) {
    return gpt_ready ? current_disk_guid : NULL;
}

enum nexus_installer_journal_result nexus_installer_load_recovery_record(
    const struct nexus_block_device *d,
    const struct nexus_installer_disk_identity *identity,
    const struct nexus_gpt_partition *part,
    struct nexus_installer_journal_record *record) {
    (void)d; (void)identity; (void)part;
    if (old_journal_result == NEXUS_JOURNAL_OK && record != NULL)
        record->phase = old_journal_phase;
    return old_journal_result;
}

enum nexus_gpt_install_result nexus_installer_write_gpt(
    const struct nexus_block_device *d,
    const struct nexus_installer_disk_identity *identity,
    const char *id, const char *erase,
    const struct nexus_partition_plan *plan,
    const uint8_t disk_guid[16], const uint8_t *partition_guids) {
    (void)identity;
    if (d != &disk || strcmp(id, disk.stable_id) != 0 ||
        strcmp(erase, "ERASE") != 0) return NEXUS_GPT_INSTALL_UNSAFE_TARGET;
    ++gpt_writes;
    if (fail_gpt) return NEXUS_GPT_INSTALL_IO_ERROR;
    populate_partitions(plan, disk_guid, partition_guids);
    return NEXUS_GPT_INSTALL_OK;
}

enum nexus_installer_journal_result nexus_installer_store_recovery_record(
    const struct nexus_block_device *d,
    const struct nexus_installer_disk_identity *identity,
    const char *id, const char *erase,
    const struct nexus_gpt_partition *part,
    const struct nexus_installer_journal_record *record) {
    (void)identity; (void)part;
    if (d != &disk || strcmp(id, disk.stable_id) != 0 ||
        strcmp(erase, "ERASE") != 0) return NEXUS_JOURNAL_INVALID;
    ++journal_writes;
    if (journal_phase_count < 8U)
        journal_phases[journal_phase_count++] = record->phase;
    return fail_journal ? NEXUS_JOURNAL_IO_ERROR : NEXUS_JOURNAL_OK;
}

enum nexus_fat32_format_result nexus_installer_format_fat32(
    const struct nexus_block_device *d,
    const struct nexus_installer_disk_identity *identity,
    const char *id, const char *erase,
    uint64_t first, uint64_t count, uint32_t volume_id) {
    (void)identity; (void)id; (void)erase; (void)first; (void)count;
    (void)volume_id;
    ++esp_formats;
    return d == &disk && !fail_esp ? NEXUS_FAT32_FORMAT_OK
                                    : NEXUS_FAT32_FORMAT_IO_ERROR;
}

bool nexus_installer_format_nxfs(
    const struct nexus_block_device *d,
    const struct nexus_installer_disk_identity *identity,
    const char *id, const char *erase,
    const struct nexus_gpt_partition *part) {
    (void)identity; (void)id; (void)erase; (void)part;
    ++system_formats;
    return d == &disk && !fail_system;
}

enum nexus_installer_system_payload_result nexus_installer_deploy_system_payload(
    const struct nexus_block_device *d,
    const struct nexus_installer_disk_identity *identity,
    const char *id, const char *erase,
    const struct nexus_gpt_partition *part) {
    (void)identity; (void)id; (void)erase; (void)part;
    ++payload_writes;
    return d == &disk && !fail_payload ? NEXUS_SYSTEM_PAYLOAD_OK
                                       : NEXUS_SYSTEM_PAYLOAD_KERNEL_WRITE_FAILED;
}

bool nexus_installer_deploy_boot_payloads(
    const struct nexus_block_device *d,
    const struct nexus_installer_disk_identity *identity,
    const char *id, const char *erase, uint64_t first, uint64_t count) {
    (void)identity; (void)id; (void)erase; (void)first; (void)count;
    ++boot_payload_writes;
    return d == &disk && !fail_boot_payload;
}

bool nexus_installer_install_bios_bootloader(
    const struct nexus_block_device *d,
    const struct nexus_installer_disk_identity *identity,
    const char *id, const char *erase,
    const struct nexus_gpt_partition *part) {
    (void)identity; (void)id; (void)erase; (void)part;
    ++bios_installs;
    return d == &disk && !fail_bios;
}

static enum nexus_installer_runtime_phase progress_seen[8];
static size_t progress_count;
static void progress_cb(void *context,
    enum nexus_installer_runtime_phase phase, const char *message) {
    (void)context; (void)message;
    if (progress_count < 8U) progress_seen[progress_count++] = phase;
}

static void reset_test(void) {
    memset(&disk, 0, sizeof(disk));
    strcpy(disk.name, "nvme-test");
    strcpy(disk.stable_id, "NVME-TEST-0001-NS1");
    strcpy(disk.model, "Nexus disposable NVMe");
    disk.block_count = 16ULL * 1024ULL * 1024ULL * 1024ULL / 512ULL;
    disk.block_size = 512U;
    disk.max_blocks_per_request = 8U;
    disk.flags = NEXUS_BLOCKDEV_READ_ONLY | NEXUS_BLOCKDEV_INSTALL_CAPABLE;
    boot_known = true; live_media = false; mounted = false;
    unique_id = true; write_capable = true; gpt_ready = false;
    fail_gpt = false; fail_journal = false; fail_esp = false;
    fail_system = false; fail_payload = false;
    fail_boot_payload = false; fail_bios = false;
    gpt_writes = journal_writes = esp_formats = system_formats = 0U;
    payload_writes = boot_payload_writes = bios_installs = 0U;
    old_journal_result = NEXUS_JOURNAL_NO_RECORD;
    old_journal_phase = NEXUS_JOURNAL_EMPTY;
    journal_phase_count = progress_count = 0U;
    memset(journal_phases, 0, sizeof(journal_phases));
    memset(progress_seen, 0, sizeof(progress_seen));
}

static enum nexus_installer_runtime_result run_install(
    enum nexus_firmware_mode mode, const char *id, const char *erase) {
    return nexus_installer_runtime_apply(&disk, mode, id, erase,
                                         progress_cb, NULL);
}

static void test_uefi_install(void) {
    reset_test();
    assert(run_install(NEXUS_FIRMWARE_UEFI, disk.stable_id, "ERASE") ==
           NEXUS_INSTALL_RESULT_OK);
    assert(gpt_writes == 1U && esp_formats == 1U && system_formats == 1U);
    assert(payload_writes == 1U && boot_payload_writes == 1U);
    assert(bios_installs == 0U);
    assert(journal_phase_count == 5U);
    assert(journal_phases[0] == NEXUS_JOURNAL_GPT_WRITTEN);
    assert(journal_phases[1] == NEXUS_JOURNAL_FILESYSTEMS_FORMATTED);
    assert(journal_phases[2] == NEXUS_JOURNAL_PAYLOAD_DEPLOYED);
    assert(journal_phases[3] == NEXUS_JOURNAL_BOOTLOADER_INSTALLED);
    assert(journal_phases[4] == NEXUS_JOURNAL_COMPLETE);
    assert(progress_count == 5U);
    assert(progress_seen[4] == NEXUS_INSTALL_PHASE_COMPLETE);
}

static void test_bios_layouts(void) {
    reset_test();
    assert(run_install(NEXUS_FIRMWARE_LEGACY_BIOS, disk.stable_id, "ERASE") ==
           NEXUS_INSTALL_RESULT_OK);
    assert(bios_installs == 1U);
    assert(current_plan.partition_count == 5U);
    assert(current_plan.partitions[0].kind == NEXUS_PARTITION_ESP);
    assert(current_plan.partitions[1].kind == NEXUS_PARTITION_BIOS_BOOT);

    reset_test();
    assert(run_install(NEXUS_FIRMWARE_DUAL, disk.stable_id, "ERASE") ==
           NEXUS_INSTALL_RESULT_OK);
    assert(bios_installs == 1U);
}

static void test_preflight_rejections(void) {
    reset_test(); boot_known = false;
    assert(run_install(NEXUS_FIRMWARE_UEFI, disk.stable_id, "ERASE") ==
           NEXUS_INSTALL_RESULT_UNSAFE_TARGET);
    assert(gpt_writes == 0U);
    reset_test(); live_media = true;
    assert(run_install(NEXUS_FIRMWARE_UEFI, disk.stable_id, "ERASE") ==
           NEXUS_INSTALL_RESULT_UNSAFE_TARGET);
    assert(gpt_writes == 0U);
    reset_test(); mounted = true;
    assert(run_install(NEXUS_FIRMWARE_UEFI, disk.stable_id, "ERASE") ==
           NEXUS_INSTALL_RESULT_UNSAFE_TARGET);
    assert(gpt_writes == 0U);
    reset_test(); unique_id = false;
    assert(run_install(NEXUS_FIRMWARE_UEFI, disk.stable_id, "ERASE") ==
           NEXUS_INSTALL_RESULT_UNSAFE_TARGET);
    assert(gpt_writes == 0U);
    reset_test(); write_capable = false;
    assert(run_install(NEXUS_FIRMWARE_UEFI, disk.stable_id, "ERASE") ==
           NEXUS_INSTALL_RESULT_UNSAFE_TARGET);
    assert(gpt_writes == 0U);
    reset_test();
    assert(run_install(NEXUS_FIRMWARE_UEFI, "WRONG-DISK", "ERASE") ==
           NEXUS_INSTALL_RESULT_UNSAFE_TARGET);
    assert(gpt_writes == 0U);
    assert(run_install(NEXUS_FIRMWARE_UEFI, disk.stable_id, "WRONG") ==
           NEXUS_INSTALL_RESULT_UNSAFE_TARGET);
    assert(gpt_writes == 0U);
}

static void test_recovery_and_failures(void) {
    reset_test();
    assert(nexus_partition_plan_create(disk.block_count, disk.block_size,
        NEXUS_FIRMWARE_UEFI, &current_plan));
    uint8_t dummy_guids[NEXUS_PLAN_MAX_PARTITIONS * 16U] = {1U};
    uint8_t dummy_disk_guid[16] = {2U};
    populate_partitions(&current_plan, dummy_disk_guid, dummy_guids);
    old_journal_result = NEXUS_JOURNAL_OK;
    old_journal_phase = NEXUS_JOURNAL_GPT_WRITTEN;
    assert(run_install(NEXUS_FIRMWARE_UEFI, disk.stable_id, "ERASE") ==
           NEXUS_INSTALL_RESULT_EXISTING_RECOVERY);
    assert(gpt_writes == 0U);

    reset_test(); fail_gpt = true;
    assert(run_install(NEXUS_FIRMWARE_UEFI, disk.stable_id, "ERASE") ==
           NEXUS_INSTALL_RESULT_PARTITION_WRITE);
    assert(journal_writes == 0U);

    reset_test(); fail_system = true;
    assert(run_install(NEXUS_FIRMWARE_UEFI, disk.stable_id, "ERASE") ==
           NEXUS_INSTALL_RESULT_SYSTEM_FORMAT);
    assert(journal_phase_count == 2U);
    assert(journal_phases[1] == NEXUS_JOURNAL_RECOVERY_REQUIRED);

    reset_test(); fail_payload = true;
    assert(run_install(NEXUS_FIRMWARE_UEFI, disk.stable_id, "ERASE") ==
           NEXUS_INSTALL_RESULT_SYSTEM_PAYLOAD);
    assert(journal_phases[journal_phase_count - 1U] ==
           NEXUS_JOURNAL_RECOVERY_REQUIRED);

    reset_test(); fail_bios = true;
    assert(run_install(NEXUS_FIRMWARE_DUAL, disk.stable_id, "ERASE") ==
           NEXUS_INSTALL_RESULT_BIOS_BOOTLOADER);
    assert(journal_phases[journal_phase_count - 1U] ==
           NEXUS_JOURNAL_RECOVERY_REQUIRED);
}

int main(void) {
    test_uefi_install();
    test_bios_layouts();
    test_preflight_rejections();
    test_recovery_and_failures();
    puts("NEXUS INSTALLER RUNTIME TESTS PASSED");
    return 0;
}
