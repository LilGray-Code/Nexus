#ifndef NEXUS_INSTALLER_GPT_IMAGE_H
#define NEXUS_INSTALLER_GPT_IMAGE_H

#include "partition_plan.h"

#include <stdbool.h>
#include <stdint.h>

#define NEXUS_GPT_IMAGE_ENTRY_COUNT 128U
#define NEXUS_GPT_IMAGE_ENTRY_SIZE 128U
#define NEXUS_GPT_IMAGE_ENTRY_BYTES \
    (NEXUS_GPT_IMAGE_ENTRY_COUNT * NEXUS_GPT_IMAGE_ENTRY_SIZE)
#define NEXUS_GPT_IMAGE_MAX_SECTOR 4096U

struct nexus_gpt_image_buffers {
    uint8_t protective_mbr[NEXUS_GPT_IMAGE_MAX_SECTOR];
    uint8_t primary_header[NEXUS_GPT_IMAGE_MAX_SECTOR];
    uint8_t backup_header[NEXUS_GPT_IMAGE_MAX_SECTOR];
    uint8_t primary_entries[NEXUS_GPT_IMAGE_ENTRY_BYTES];
    uint8_t backup_entries[NEXUS_GPT_IMAGE_ENTRY_BYTES];
};

/*
 * Serializes a complete protective MBR and mirrored GPT metadata into caller
 * memory. No disk I/O is performed. The caller supplies disk and partition
 * GUIDs; all GUIDs must be nonzero and unique.
 */
bool nexus_gpt_image_build(const struct nexus_partition_plan *plan,
                           const uint8_t disk_guid[16],
                           const uint8_t *partition_guids,
                           struct nexus_gpt_image_buffers *out);

#endif
