#include "installer_runtime.h"

#include "gpt.h"
#include "installer_storage.h"
#include "installer_journal.h"
#include "timer.h"

#include <stddef.h>
#include <stdint.h>

static size_t bounded_length(const char *s, size_t capacity) {
    if (s == NULL) return capacity;
    size_t n = 0U;
    while (n < capacity && s[n] != '\0') ++n;
    return n;
}

static bool exact_case_insensitive(const char *a, const char *b,
                                   size_t capacity) {
    if (a == NULL || b == NULL) return false;
    const size_t an = bounded_length(a, capacity);
    const size_t bn = bounded_length(b, capacity);
    if (an >= capacity || an != bn) return false;
    for (size_t i = 0U; i < an; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x = (char)(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z') y = (char)(y - 'a' + 'A');
        if (x != y) return false;
    }
    return true;
}

static bool copy_text(char *out, size_t capacity, const char *in) {
    if (out == NULL || in == NULL || capacity == 0U) return false;
    const size_t n = bounded_length(in, capacity);
    if (n == 0U || n >= capacity) return false;
    for (size_t i = 0U; i <= n; ++i) out[i] = in[i];
    return true;
}

static uint64_t mix64(uint64_t x) {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
}

static void make_guid(uint64_t *state, uint8_t out[16]) {
    uint64_t a = mix64(*state + 0x9E3779B97F4A7C15ULL);
    uint64_t b = mix64(a + 0xD1B54A32D192ED03ULL);
    *state = b;
    for (size_t i = 0U; i < 8U; ++i) {
        out[i] = (uint8_t)(a >> (i * 8U));
        out[i + 8U] = (uint8_t)(b >> (i * 8U));
    }
    out[6] = (uint8_t)((out[6] & 0x0FU) | 0x40U);
    out[8] = (uint8_t)((out[8] & 0x3FU) | 0x80U);
}

static void report(nexus_installer_progress_fn callback, void *context,
                   enum nexus_installer_runtime_phase phase,
                   const char *message) {
    if (callback != NULL) callback(context, phase, message);
}

static bool find_partition_kind(const struct nexus_partition_plan *plan,
                                const struct nexus_gpt_partition *parts,
                                enum nexus_partition_kind kind,
                                struct nexus_gpt_partition *out) {
    if (plan == NULL || parts == NULL || out == NULL) return false;
    for (size_t i = 0U; i < plan->partition_count; ++i) {
        if (plan->partitions[i].kind == kind) {
            *out = parts[i];
            return true;
        }
    }
    return false;
}

static bool old_recovery_pending(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *identity) {
    static const uint8_t recovery_guid[16] = {
        0x4EU,0x58U,0x52U,0x43U,0x01U,0x00U,0x58U,0x4EU,
        0x9AU,0x11U,0x4EU,0x58U,0x55U,0x53U,0x52U,0x43U
    };
    if (!gpt_scan_device(device)) return false;
    for (size_t i = 0U; i < gpt_partition_count(); ++i) {
        const struct nexus_gpt_partition *part = gpt_partition_at(i);
        if (part == NULL) continue;
        bool match = true;
        for (size_t b = 0U; b < 16U; ++b)
            if (part->type_guid[b] != recovery_guid[b]) match = false;
        if (!match) continue;
        struct nexus_gpt_partition recovery = *part;
        struct nexus_installer_journal_record record;
        const enum nexus_installer_journal_result result =
            nexus_installer_load_recovery_record(device, identity,
                                                  &recovery, &record);
        if (result == NEXUS_JOURNAL_NO_RECORD) return false;
        if (result != NEXUS_JOURNAL_OK) return true;
        return record.phase != NEXUS_JOURNAL_COMPLETE;
    }
    return false;
}

static bool journal_commit(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *identity,
    const char *typed_id, const char *typed_erase,
    const struct nexus_gpt_partition *recovery,
    struct nexus_installer_journal_record *record,
    enum nexus_installer_journal_phase phase) {
    if (record == NULL || recovery == NULL) return false;
    if (record->sequence == UINT64_MAX) return false;
    ++record->sequence;
    record->phase = phase;
    ++record->completed_steps;
    return nexus_installer_store_recovery_record(
        device, identity, typed_id, typed_erase, recovery, record) ==
        NEXUS_JOURNAL_OK;
}

static void journal_require_recovery(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *identity,
    const char *typed_id, const char *typed_erase,
    const struct nexus_gpt_partition *recovery,
    struct nexus_installer_journal_record *record) {
    if (record == NULL || recovery == NULL || record->sequence == UINT64_MAX)
        return;
    ++record->sequence;
    record->phase = NEXUS_JOURNAL_RECOVERY_REQUIRED;
    (void)nexus_installer_store_recovery_record(
        device, identity, typed_id, typed_erase, recovery, record);
}

static enum nexus_installer_runtime_result apply_remaining_stages(
    const struct nexus_block_device *device,
    const struct nexus_installer_disk_identity *identity,
    const char *typed_id, const char *typed_erase,
    enum nexus_firmware_mode firmware,
    const struct nexus_gpt_partition *esp,
    const struct nexus_gpt_partition *system,
    const struct nexus_gpt_partition *data,
    const struct nexus_gpt_partition *recovery,
    const struct nexus_gpt_partition *bios,
    struct nexus_installer_journal_record *journal,
    nexus_installer_progress_fn progress, void *progress_context) {
    if (device == NULL || identity == NULL || esp == NULL ||
        system == NULL || data == NULL || recovery == NULL ||
        journal == NULL || journal->completed_steps < 1U ||
        journal->completed_steps > 5U)
        return NEXUS_INSTALL_RESULT_EXISTING_RECOVERY;

    const bool needs_bios = firmware != NEXUS_FIRMWARE_UEFI;
    const uint64_t esp_blocks = esp->last_lba - esp->first_lba + 1U;
    uint32_t volume_id = (uint32_t)journal->disk_guid[0] |
        ((uint32_t)journal->disk_guid[1] << 8) |
        ((uint32_t)journal->disk_guid[2] << 16) |
        ((uint32_t)journal->disk_guid[3] << 24);
    if (volume_id == 0U) volume_id = 1U;

    if (journal->completed_steps < 2U) {
        report(progress, progress_context, NEXUS_INSTALL_PHASE_FORMATTING,
               "Formatting the EFI system partition");
        if (nexus_installer_format_fat32(device, identity, typed_id,
            typed_erase, esp->first_lba, esp_blocks, volume_id) !=
            NEXUS_FAT32_FORMAT_OK) {
            journal_require_recovery(device, identity, typed_id, typed_erase,
                                     recovery, journal);
            return NEXUS_INSTALL_RESULT_ESP_FORMAT;
        }
        report(progress, progress_context, NEXUS_INSTALL_PHASE_FORMATTING,
               "Formatting the Nexus system and data volumes");
        if (!nexus_installer_format_nxfs(device, identity, typed_id,
            typed_erase, system) ||
            !nexus_installer_format_nxfs(device, identity, typed_id,
            typed_erase, data)) {
            journal_require_recovery(device, identity, typed_id, typed_erase,
                                     recovery, journal);
            return NEXUS_INSTALL_RESULT_SYSTEM_FORMAT;
        }
        if (!journal_commit(device, identity, typed_id, typed_erase,
            recovery, journal, NEXUS_JOURNAL_FILESYSTEMS_FORMATTED)) {
            journal_require_recovery(device, identity, typed_id, typed_erase,
                                     recovery, journal);
            return NEXUS_INSTALL_RESULT_JOURNAL;
        }
    }

    if (journal->completed_steps < 3U) {
        report(progress, progress_context, NEXUS_INSTALL_PHASE_DEPLOYING,
               "Deploying the Nexus kernel and system manifest");
        if (nexus_installer_deploy_system_payload(device, identity,
            typed_id, typed_erase, system) != NEXUS_SYSTEM_PAYLOAD_OK) {
            journal_require_recovery(device, identity, typed_id, typed_erase,
                                     recovery, journal);
            return NEXUS_INSTALL_RESULT_SYSTEM_PAYLOAD;
        }
        if (!nexus_installer_deploy_boot_payloads(device, identity,
            typed_id, typed_erase, esp->first_lba, esp_blocks)) {
            journal_require_recovery(device, identity, typed_id, typed_erase,
                                     recovery, journal);
            return NEXUS_INSTALL_RESULT_BOOT_PAYLOAD;
        }
        if (!journal_commit(device, identity, typed_id, typed_erase,
            recovery, journal, NEXUS_JOURNAL_PAYLOAD_DEPLOYED)) {
            journal_require_recovery(device, identity, typed_id, typed_erase,
                                     recovery, journal);
            return NEXUS_INSTALL_RESULT_JOURNAL;
        }
    }

    if (journal->completed_steps < 4U) {
        report(progress, progress_context, NEXUS_INSTALL_PHASE_BOOTLOADER,
               "Installing the legacy BIOS boot stage when requested");
        if (needs_bios && (bios == NULL ||
            !nexus_installer_install_bios_bootloader(device, identity,
                typed_id, typed_erase, bios))) {
            journal_require_recovery(device, identity, typed_id, typed_erase,
                                     recovery, journal);
            return NEXUS_INSTALL_RESULT_BIOS_BOOTLOADER;
        }
        if (!journal_commit(device, identity, typed_id, typed_erase,
            recovery, journal, NEXUS_JOURNAL_BOOTLOADER_INSTALLED)) {
            journal_require_recovery(device, identity, typed_id, typed_erase,
                                     recovery, journal);
            return NEXUS_INSTALL_RESULT_JOURNAL;
        }
    }

    if (journal->completed_steps < 5U) {
        if (!journal_commit(device, identity, typed_id, typed_erase,
            recovery, journal, NEXUS_JOURNAL_COMPLETE)) {
            journal_require_recovery(device, identity, typed_id, typed_erase,
                                     recovery, journal);
            return NEXUS_INSTALL_RESULT_JOURNAL;
        }
    } else if (journal->phase != NEXUS_JOURNAL_COMPLETE) {
        if (journal->sequence == UINT64_MAX)
            return NEXUS_INSTALL_RESULT_JOURNAL;
        ++journal->sequence;
        journal->phase = NEXUS_JOURNAL_COMPLETE;
        if (nexus_installer_store_recovery_record(device, identity,
            typed_id, typed_erase, recovery, journal) != NEXUS_JOURNAL_OK) {
            journal_require_recovery(device, identity, typed_id, typed_erase,
                                     recovery, journal);
            return NEXUS_INSTALL_RESULT_JOURNAL;
        }
    }

    report(progress, progress_context, NEXUS_INSTALL_PHASE_COMPLETE,
           "Nexus installation payload and bootloader deployment completed");
    return NEXUS_INSTALL_RESULT_OK;
}

enum nexus_installer_runtime_result nexus_installer_runtime_apply(
    const struct nexus_block_device *device,
    enum nexus_firmware_mode firmware,
    const char *typed_stable_id,
    const char *typed_erase_phrase,
    nexus_installer_progress_fn progress,
    void *progress_context) {
    if (device == NULL || typed_stable_id == NULL ||
        typed_erase_phrase == NULL ||
        (firmware != NEXUS_FIRMWARE_UEFI &&
         firmware != NEXUS_FIRMWARE_LEGACY_BIOS &&
         firmware != NEXUS_FIRMWARE_DUAL))
        return NEXUS_INSTALL_RESULT_INVALID_ARGUMENT;

    report(progress, progress_context, NEXUS_INSTALL_PHASE_PREFLIGHT,
           "Checking selected disk identity and boot-medium safety");
    if (!blockdev_boot_media_detection_complete() ||
        blockdev_is_boot_media(device) || blockdev_is_mounted(device) ||
        !blockdev_identity_is_unique(device) ||
        !blockdev_install_write_supported(device) ||
        !exact_case_insensitive(device->stable_id, typed_stable_id,
                                NEXUS_BLOCKDEV_ID_MAX) ||
        !exact_case_insensitive("ERASE", typed_erase_phrase, 8U) ||
        !blockdev_matches_identity(device, device->stable_id, device->model,
                                   device->block_count, device->block_size))
        return NEXUS_INSTALL_RESULT_UNSAFE_TARGET;

    struct nexus_installer_disk_identity identity = {0};
    if (!copy_text(identity.stable_id, sizeof(identity.stable_id),
                   device->stable_id) ||
        !copy_text(identity.model, sizeof(identity.model), device->model))
        return NEXUS_INSTALL_RESULT_INVALID_ARGUMENT;
    identity.block_count = device->block_count;
    identity.block_size = device->block_size;

    if (old_recovery_pending(device, &identity))
        return nexus_installer_runtime_recover(device, typed_stable_id,
            typed_erase_phrase, progress, progress_context);

    /* BIOS installs retain an ESP for Limine's config and boot files. */
    const enum nexus_firmware_mode layout =
        firmware == NEXUS_FIRMWARE_UEFI
            ? NEXUS_FIRMWARE_UEFI : NEXUS_FIRMWARE_DUAL;
    struct nexus_partition_plan plan;
    if (!nexus_partition_plan_create(device->block_count, device->block_size,
                                     layout, &plan))
        return NEXUS_INSTALL_RESULT_UNSUPPORTED_LAYOUT;

    uint64_t seed = timer_ticks() ^ device->block_count ^
                    ((uint64_t)device->block_size << 32);
    for (size_t i = 0U; device->stable_id[i] != '\0'; ++i)
        seed = mix64(seed ^ (uint8_t)device->stable_id[i]);
    uint8_t disk_guid[16];
    uint8_t partition_guids[NEXUS_PLAN_MAX_PARTITIONS * 16U];
    make_guid(&seed, disk_guid);
    for (size_t i = 0U; i < plan.partition_count; ++i)
        make_guid(&seed, partition_guids + i * 16U);

    report(progress, progress_context, NEXUS_INSTALL_PHASE_PARTITIONING,
           "Writing and verifying the GPT partition table");
    const enum nexus_gpt_install_result gpt_result =
        nexus_installer_write_gpt(device, &identity, typed_stable_id,
            typed_erase_phrase, &plan, disk_guid, partition_guids);
    if (gpt_result != NEXUS_GPT_INSTALL_OK)
        return NEXUS_INSTALL_RESULT_PARTITION_WRITE;
    if (!gpt_scan_device(device) ||
        gpt_partition_count() != plan.partition_count)
        return NEXUS_INSTALL_RESULT_PARTITION_VERIFY;

    struct nexus_gpt_partition partitions[NEXUS_PLAN_MAX_PARTITIONS];
    for (size_t i = 0U; i < plan.partition_count; ++i) {
        const struct nexus_gpt_partition *part = gpt_partition_at(i);
        if (part == NULL) return NEXUS_INSTALL_RESULT_PARTITION_VERIFY;
        partitions[i] = *part;
    }

    struct nexus_gpt_partition esp, system, recovery, data, bios;
    if (!find_partition_kind(&plan, partitions, NEXUS_PARTITION_ESP, &esp) ||
        !find_partition_kind(&plan, partitions, NEXUS_PARTITION_SYSTEM, &system) ||
        !find_partition_kind(&plan, partitions, NEXUS_PARTITION_RECOVERY,
                             &recovery) ||
        !find_partition_kind(&plan, partitions, NEXUS_PARTITION_DATA, &data))
        return NEXUS_INSTALL_RESULT_PARTITION_VERIFY;
    const bool needs_bios = firmware != NEXUS_FIRMWARE_UEFI;
    if (needs_bios &&
        !find_partition_kind(&plan, partitions, NEXUS_PARTITION_BIOS_BOOT,
                             &bios))
        return NEXUS_INSTALL_RESULT_PARTITION_VERIFY;

    struct nexus_installer_journal_record journal = {0};
    journal.sequence = 1U;
    journal.phase = NEXUS_JOURNAL_GPT_WRITTEN;
    journal.disk_block_count = identity.block_count;
    journal.block_size = identity.block_size;
    journal.firmware_mode = (uint32_t)firmware;
    journal.completed_steps = 1U;
    if (!copy_text(journal.stable_id, sizeof(journal.stable_id),
                   identity.stable_id) ||
        !copy_text(journal.model, sizeof(journal.model), identity.model))
        return NEXUS_INSTALL_RESULT_INVALID_ARGUMENT;
    const uint8_t *scanned_guid = gpt_scanned_disk_guid();
    if (scanned_guid == NULL) return NEXUS_INSTALL_RESULT_PARTITION_VERIFY;
    for (size_t i = 0U; i < 16U; ++i) journal.disk_guid[i] = scanned_guid[i];
    if (nexus_installer_store_recovery_record(device, &identity,
        typed_stable_id, typed_erase_phrase, &recovery, &journal) !=
        NEXUS_JOURNAL_OK)
        return NEXUS_INSTALL_RESULT_JOURNAL;

    return apply_remaining_stages(device, &identity, typed_stable_id,
        typed_erase_phrase, firmware, &esp, &system, &data, &recovery,
        needs_bios ? &bios : NULL, &journal, progress, progress_context);
}



enum nexus_installer_runtime_result nexus_installer_runtime_recover(
    const struct nexus_block_device *device,
    const char *typed_stable_id,
    const char *typed_erase_phrase,
    nexus_installer_progress_fn progress,
    void *progress_context) {
    static const uint8_t recovery_guid[16] = {
        0x4EU,0x58U,0x52U,0x43U,0x01U,0x00U,0x58U,0x4EU,
        0x9AU,0x11U,0x4EU,0x58U,0x55U,0x53U,0x52U,0x43U
    };
    if (device == NULL || typed_stable_id == NULL ||
        typed_erase_phrase == NULL)
        return NEXUS_INSTALL_RESULT_INVALID_ARGUMENT;
    if (!blockdev_boot_media_detection_complete() ||
        blockdev_is_boot_media(device) || blockdev_is_mounted(device) ||
        !blockdev_identity_is_unique(device) ||
        !blockdev_install_write_supported(device) ||
        !exact_case_insensitive(device->stable_id, typed_stable_id,
                                NEXUS_BLOCKDEV_ID_MAX) ||
        !exact_case_insensitive("ERASE", typed_erase_phrase, 8U) ||
        !blockdev_matches_identity(device, device->stable_id, device->model,
                                   device->block_count, device->block_size))
        return NEXUS_INSTALL_RESULT_UNSAFE_TARGET;

    struct nexus_installer_disk_identity identity = {0};
    if (!copy_text(identity.stable_id, sizeof(identity.stable_id),
                   device->stable_id) ||
        !copy_text(identity.model, sizeof(identity.model), device->model))
        return NEXUS_INSTALL_RESULT_INVALID_ARGUMENT;
    identity.block_count = device->block_count;
    identity.block_size = device->block_size;

    report(progress, progress_context, NEXUS_INSTALL_PHASE_PREFLIGHT,
           "Validating the interrupted installation and recovery journal");
    if (!gpt_scan_device(device))
        return NEXUS_INSTALL_RESULT_EXISTING_RECOVERY;
    struct nexus_gpt_partition recovery = {0};
    bool found_recovery = false;
    for (size_t i = 0U; i < gpt_partition_count(); ++i) {
        const struct nexus_gpt_partition *part = gpt_partition_at(i);
        if (part == NULL) continue;
        bool match = true;
        for (size_t b = 0U; b < 16U; ++b)
            if (part->type_guid[b] != recovery_guid[b]) match = false;
        if (match) {
            recovery = *part;
            found_recovery = true;
            break;
        }
    }
    if (!found_recovery) return NEXUS_INSTALL_RESULT_EXISTING_RECOVERY;

    uint8_t actual_disk_guid[16];
    const uint8_t *scanned_guid = gpt_scanned_disk_guid();
    if (scanned_guid == NULL) return NEXUS_INSTALL_RESULT_EXISTING_RECOVERY;
    for (size_t i = 0U; i < 16U; ++i) actual_disk_guid[i] = scanned_guid[i];

    struct nexus_installer_journal_record journal = {0};
    if (nexus_installer_load_recovery_record(device, &identity, &recovery,
        &journal) != NEXUS_JOURNAL_OK)
        return NEXUS_INSTALL_RESULT_EXISTING_RECOVERY;
    if (!exact_case_insensitive(journal.stable_id, identity.stable_id,
                                sizeof(journal.stable_id)) ||
        !exact_case_insensitive(journal.model, identity.model,
                                sizeof(journal.model)) ||
        journal.disk_block_count != identity.block_count ||
        journal.block_size != identity.block_size)
        return NEXUS_INSTALL_RESULT_EXISTING_RECOVERY;
    for (size_t i = 0U; i < 16U; ++i)
        if (journal.disk_guid[i] != actual_disk_guid[i])
            return NEXUS_INSTALL_RESULT_EXISTING_RECOVERY;

    const enum nexus_firmware_mode firmware =
        (enum nexus_firmware_mode)journal.firmware_mode;
    if (firmware != NEXUS_FIRMWARE_UEFI &&
        firmware != NEXUS_FIRMWARE_LEGACY_BIOS &&
        firmware != NEXUS_FIRMWARE_DUAL)
        return NEXUS_INSTALL_RESULT_EXISTING_RECOVERY;
    if (journal.phase == NEXUS_JOURNAL_COMPLETE) {
        if (journal.completed_steps != 5U)
            return NEXUS_INSTALL_RESULT_EXISTING_RECOVERY;
        report(progress, progress_context, NEXUS_INSTALL_PHASE_COMPLETE,
               "The journal confirms this installation is already complete");
        return NEXUS_INSTALL_RESULT_OK;
    }
    if (journal.completed_steps < 1U || journal.completed_steps > 5U ||
        (journal.phase != NEXUS_JOURNAL_RECOVERY_REQUIRED &&
         journal.phase != NEXUS_JOURNAL_GPT_WRITTEN &&
         journal.phase != NEXUS_JOURNAL_FILESYSTEMS_FORMATTED &&
         journal.phase != NEXUS_JOURNAL_PAYLOAD_DEPLOYED &&
         journal.phase != NEXUS_JOURNAL_BOOTLOADER_INSTALLED))
        return NEXUS_INSTALL_RESULT_EXISTING_RECOVERY;

    const enum nexus_firmware_mode layout =
        firmware == NEXUS_FIRMWARE_UEFI
            ? NEXUS_FIRMWARE_UEFI : NEXUS_FIRMWARE_DUAL;
    struct nexus_partition_plan plan;
    if (!nexus_partition_plan_create(identity.block_count, identity.block_size,
                                     layout, &plan) ||
        !gpt_scan_device(device) ||
        gpt_partition_count() != plan.partition_count)
        return NEXUS_INSTALL_RESULT_EXISTING_RECOVERY;

    struct nexus_gpt_partition partitions[NEXUS_PLAN_MAX_PARTITIONS];
    for (size_t i = 0U; i < plan.partition_count; ++i) {
        const struct nexus_gpt_partition *part = gpt_partition_at(i);
        if (part == NULL ||
            part->first_lba != plan.partitions[i].first_lba ||
            part->last_lba != plan.partitions[i].last_lba)
            return NEXUS_INSTALL_RESULT_EXISTING_RECOVERY;
        partitions[i] = *part;
    }

    struct nexus_gpt_partition esp, system, data, bios;
    if (!find_partition_kind(&plan, partitions, NEXUS_PARTITION_ESP, &esp) ||
        !find_partition_kind(&plan, partitions, NEXUS_PARTITION_SYSTEM, &system) ||
        !find_partition_kind(&plan, partitions, NEXUS_PARTITION_DATA, &data))
        return NEXUS_INSTALL_RESULT_EXISTING_RECOVERY;
    const bool needs_bios = firmware != NEXUS_FIRMWARE_UEFI;
    if (needs_bios &&
        !find_partition_kind(&plan, partitions, NEXUS_PARTITION_BIOS_BOOT,
                             &bios))
        return NEXUS_INSTALL_RESULT_EXISTING_RECOVERY;

    report(progress, progress_context, NEXUS_INSTALL_PHASE_RECOVERY,
           "Resuming from the last verified installer journal phase");
    return apply_remaining_stages(device, &identity, typed_stable_id,
        typed_erase_phrase, firmware, &esp, &system, &data, &recovery,
        needs_bios ? &bios : NULL, &journal, progress, progress_context);
}
