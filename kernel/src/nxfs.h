#ifndef NEXUS_NXFS_H
#define NEXUS_NXFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "blockdev.h"
#include "gpt.h"

#define NEXUS_NXFS_MAX_FILES 16U
#define NEXUS_NXFS_MAX_FILE_BYTES (16U * 1024U)
#define NEXUS_NXFS_NAME_MAX 13U

struct nexus_nxfs_entry {
    bool used;
    uint8_t bank;
    char name[NEXUS_NXFS_NAME_MAX];
    uint32_t size;
    uint32_t data_crc;
    uint64_t generation;
};

bool nxfs_partition_type_matches(const struct nexus_gpt_partition *partition);
bool nxfs_format_install(const struct nexus_block_device *device,
                         const struct nexus_gpt_partition *partition,
                         const char *typed_stable_id,
                         const char *typed_erase_phrase);
bool nxfs_format(const struct nexus_block_device *device,
                 const struct nexus_gpt_partition *partition);
bool nxfs_mount(const struct nexus_block_device *device,
                const struct nexus_gpt_partition *partition);
bool nxfs_is_mounted(void);
bool nxfs_is_writable(void);
size_t nxfs_entry_count(void);
const struct nexus_nxfs_entry *nxfs_entry_at(size_t index);
bool nxfs_read_file(const struct nexus_nxfs_entry *entry,
                    uint32_t offset, uint32_t length, void *buffer);
bool nxfs_write_file(const char *name, const void *data, size_t length);
bool nxfs_remove_file(const char *name);
bool nxfs_selftest(void);

#endif
