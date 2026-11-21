#ifndef NEXUS_VFS_H
#define NEXUS_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "blockdev.h"
#include "gpt.h"

#define NEXUS_VFS_NAME_MAX 13U
#define NEXUS_VFS_READ_CHUNK 4096U

enum nexus_vfs_node_type {
    NEXUS_VFS_FILE = 1,
    NEXUS_VFS_DIRECTORY = 2
};

struct nexus_vfs_dirent {
    char name[NEXUS_VFS_NAME_MAX];
    enum nexus_vfs_node_type type;
    uint32_t size;
};

struct nexus_vfs_file {
    const void *filesystem_entry;
    uint32_t offset;
    bool open;
};

bool vfs_mount_root_fat32(const struct nexus_block_device *device,
                          const struct nexus_gpt_partition *partition);
bool vfs_root_mounted(void);
size_t vfs_root_entry_count(void);
bool vfs_root_entry(size_t index, struct nexus_vfs_dirent *entry);
bool vfs_open_readonly(const char *name, struct nexus_vfs_file *file);
bool vfs_read(struct nexus_vfs_file *file, void *buffer,
              size_t requested, size_t *bytes_read);
void vfs_close(struct nexus_vfs_file *file);

#endif
