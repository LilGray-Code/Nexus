#ifndef NEXUS_INSTALLER_JOURNAL_H
#define NEXUS_INSTALLER_JOURNAL_H

#include <stdbool.h>
#include <stdint.h>

#define NEXUS_INSTALLER_JOURNAL_ID_MAX 64U
#define NEXUS_INSTALLER_JOURNAL_MODEL_MAX 64U

enum nexus_installer_journal_phase {
    NEXUS_JOURNAL_EMPTY = 0,
    NEXUS_JOURNAL_PREPARED = 1,
    NEXUS_JOURNAL_GPT_WRITTEN = 2,
    NEXUS_JOURNAL_FILESYSTEMS_FORMATTED = 3,
    NEXUS_JOURNAL_PAYLOAD_DEPLOYED = 4,
    NEXUS_JOURNAL_BOOTLOADER_INSTALLED = 5,
    NEXUS_JOURNAL_COMPLETE = 6,
    NEXUS_JOURNAL_RECOVERY_REQUIRED = 7
};

enum nexus_installer_journal_result {
    NEXUS_JOURNAL_OK = 0,
    NEXUS_JOURNAL_NO_RECORD,
    NEXUS_JOURNAL_CORRUPT,
    NEXUS_JOURNAL_IO_ERROR,
    NEXUS_JOURNAL_INVALID
};

struct nexus_installer_journal_record {
    uint64_t sequence;
    enum nexus_installer_journal_phase phase;
    uint64_t disk_block_count;
    uint32_t block_size;
    uint32_t firmware_mode;
    uint32_t completed_steps;
    char stable_id[NEXUS_INSTALLER_JOURNAL_ID_MAX];
    char model[NEXUS_INSTALLER_JOURNAL_MODEL_MAX];
    uint8_t disk_guid[16];
};

struct nexus_installer_journal_device {
    uint64_t block_count;
    uint32_t block_size;
    uint32_t max_blocks_per_request;
    void *context;
    bool (*read)(void *context, uint64_t lba, uint32_t blocks, void *buffer);
    bool (*write)(void *context, uint64_t lba, uint32_t blocks,
                  const void *buffer);
    bool (*flush)(void *context);
};

/* The two journal sectors must be in a reserved region, not in active FAT
 * metadata or file data. Nexus stores alternating records in ESP-relative
 * sectors 8 and 9, which remain reserved in the FAT32 layout. Each commit is
 * flushed and verified so a torn write leaves the previous record intact. */
enum nexus_installer_journal_result nexus_installer_journal_load(
    const struct nexus_installer_journal_device *device, uint64_t first_lba,
    struct nexus_installer_journal_record *record);
enum nexus_installer_journal_result nexus_installer_journal_commit(
    const struct nexus_installer_journal_device *device, uint64_t first_lba,
    const struct nexus_installer_journal_record *record);

#endif
