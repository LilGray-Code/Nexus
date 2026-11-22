#ifndef NEXUS_INSTALLER_RUNTIME_H
#define NEXUS_INSTALLER_RUNTIME_H

#include "blockdev.h"
#include "partition_plan.h"

#include <stdbool.h>

enum nexus_installer_runtime_phase {
    NEXUS_INSTALL_PHASE_PREFLIGHT = 0,
    NEXUS_INSTALL_PHASE_PARTITIONING,
    NEXUS_INSTALL_PHASE_FORMATTING,
    NEXUS_INSTALL_PHASE_DEPLOYING,
    NEXUS_INSTALL_PHASE_BOOTLOADER,
    NEXUS_INSTALL_PHASE_COMPLETE,
    NEXUS_INSTALL_PHASE_RECOVERY
};

enum nexus_installer_runtime_result {
    NEXUS_INSTALL_RESULT_OK = 0,
    NEXUS_INSTALL_RESULT_INVALID_ARGUMENT,
    NEXUS_INSTALL_RESULT_UNSAFE_TARGET,
    NEXUS_INSTALL_RESULT_UNSUPPORTED_LAYOUT,
    NEXUS_INSTALL_RESULT_EXISTING_RECOVERY,
    NEXUS_INSTALL_RESULT_PARTITION_WRITE,
    NEXUS_INSTALL_RESULT_PARTITION_VERIFY,
    NEXUS_INSTALL_RESULT_JOURNAL,
    NEXUS_INSTALL_RESULT_ESP_FORMAT,
    NEXUS_INSTALL_RESULT_SYSTEM_FORMAT,
    NEXUS_INSTALL_RESULT_SYSTEM_PAYLOAD,
    NEXUS_INSTALL_RESULT_BOOT_PAYLOAD,
    NEXUS_INSTALL_RESULT_BIOS_BOOTLOADER
};

typedef void (*nexus_installer_progress_fn)(
    void *context, enum nexus_installer_runtime_phase phase,
    const char *message);

/*
 * Runs the complete first-install transaction against one explicitly selected
 * disk. The caller must obtain two separate user confirmations and pass the
 * exact stable ID plus the typed ERASE phrase. The kernel revalidates identity,
 * boot-medium provenance, mounted state, and write capability before writes.
 *
 * Legacy BIOS uses the dual-layout partition plan because Limine's BIOS stage
 * needs the FAT32 ESP for its configuration and boot payloads.
 */
enum nexus_installer_runtime_result nexus_installer_runtime_apply(
    const struct nexus_block_device *device,
    enum nexus_firmware_mode firmware,
    const char *typed_stable_id,
    const char *typed_erase_phrase,
    nexus_installer_progress_fn progress,
    void *progress_context);

/* Resumes a previously journaled installation without rewriting its GPT. */
enum nexus_installer_runtime_result nexus_installer_runtime_recover(
    const struct nexus_block_device *device,
    const char *typed_stable_id,
    const char *typed_erase_phrase,
    nexus_installer_progress_fn progress,
    void *progress_context);

#endif
