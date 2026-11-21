#ifndef NEXUS_INSTALLER_GPT_INSTALL_H
#define NEXUS_INSTALLER_GPT_INSTALL_H

#include "gpt_image.h"

#include <stdbool.h>
#include <stdint.h>

typedef bool (*nexus_gpt_read_fn)(void *context, uint64_t lba,
                                  uint32_t block_count, void *buffer);
typedef bool (*nexus_gpt_write_fn)(void *context, uint64_t lba,
                                   uint32_t block_count, const void *buffer);
typedef bool (*nexus_gpt_flush_fn)(void *context);

struct nexus_gpt_install_target {
    uint64_t block_count;
    uint32_t block_size;
    uint32_t max_blocks_per_request;
    void *context;
    nexus_gpt_read_fn read;
    nexus_gpt_write_fn write;
    nexus_gpt_flush_fn flush;
    bool identity_revalidated;
    bool not_live_media;
    bool not_mounted;
};

enum nexus_gpt_install_result {
    NEXUS_GPT_INSTALL_OK = 0,
    NEXUS_GPT_INSTALL_INVALID = 1,
    NEXUS_GPT_INSTALL_NOT_CONFIRMED = 2,
    NEXUS_GPT_INSTALL_UNSAFE_TARGET = 3,
    NEXUS_GPT_INSTALL_IO_ERROR = 4,
    NEXUS_GPT_INSTALL_VERIFY_ERROR = 5
};

/*
 * Writes only GPT metadata, not filesystems or system contents. Requires two
 * independent confirmations plus a freshly revalidated, unmounted, non-live
 * target. Metadata order is backup entries/header, primary entries/header,
 * then the protective MBR. Every phase is flushed and read back.
 *
 * This low-level transaction is not yet wired to the desktop UI. It is
 * exercised with an in-memory mock target in CI only.
 */
enum nexus_gpt_install_result nexus_gpt_install_apply(
    const struct nexus_gpt_install_target *target,
    const struct nexus_partition_plan *plan,
    const uint8_t disk_guid[16],
    const uint8_t *partition_guids,
    bool first_confirmation,
    bool second_confirmation);

#endif
