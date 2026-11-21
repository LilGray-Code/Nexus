#ifndef NEXUS_FAT32_H
#define NEXUS_FAT32_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "blockdev.h"
#include "gpt.h"

#define NEXUS_FAT32_MAX_ROOT_ENTRIES 32U
#define NEXUS_FAT32_MAX_READ_BYTES 4096U

struct nexus_fat32_entry {
    char name[13];
    uint8_t attributes;
    uint32_t first_cluster;
    uint32_t size;
};

bool fat32_mount(const struct nexus_block_device *device,
                 const struct nexus_gpt_partition *partition);
size_t fat32_root_entry_count(void);
const struct nexus_fat32_entry *fat32_root_entry_at(size_t index);
bool fat32_read_file(const struct nexus_fat32_entry *entry,
                     uint32_t offset, uint32_t length, void *buffer);

#endif
