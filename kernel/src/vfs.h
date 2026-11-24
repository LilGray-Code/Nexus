#ifndef NEXUS_VFS_H
#define NEXUS_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "blockdev.h"
#include "gpt.h"
#include "fat32.h"

#define NEXUS_VFS_NAME_MAX 32U /* 31 printable characters plus NUL */
#define NEXUS_VFS_READ_CHUNK 4096U
#define NEXUS_VFS_RAM_MAX_FILES 16U
#define NEXUS_VFS_RAM_MAX_FILE_SIZE 4096U

enum nexus_vfs_node_type {
    NEXUS_VFS_FILE = 1,
    NEXUS_VFS_DIRECTORY = 2
};

struct nexus_vfs_dirent {
    char name[NEXUS_VFS_NAME_MAX];
    enum nexus_vfs_node_type type;
    uint32_t size;
    uint32_t first_cluster;
};

struct nexus_vfs_file {
    const void *filesystem_entry;
    uint32_t offset;
    bool open;
    bool ram_file;
    bool nxfs_file;
    bool fat32_file;
    struct nexus_fat32_entry fat32_snapshot;
};

bool vfs_mount_root_fat32(const struct nexus_block_device *device,
                          const struct nexus_gpt_partition *partition);
bool vfs_mount_root_nxfs(const struct nexus_block_device *device,
                         const struct nexus_gpt_partition *partition);
bool vfs_nxfs_mounted(void);
bool vfs_nxfs_writable(void);
bool vfs_save_ram_file_to_nxfs(const char *name);
bool vfs_write_file(const char *name, const void *data, size_t length);
bool vfs_root_mounted(void);
size_t vfs_directory_depth(void);
bool vfs_current_path(char *buffer, size_t capacity);
bool vfs_enter_directory(const char *name);
bool vfs_parent_directory(void);
bool vfs_file_writable(const char *name);
bool vfs_current_directory_writable(void);
bool vfs_create_directory(const char *name);
bool vfs_remove_directory(const char *name);
bool vfs_has_filesystem(void);
bool vfs_ramfs_create(const char *name);
bool vfs_ramfs_write(const char *name, const void *data, size_t length);
bool vfs_ramfs_remove(const char *name);
bool vfs_remove_file(const char *name);
bool vfs_ramfs_selftest(void);
size_t vfs_root_entry_count(void);
bool vfs_root_entry(size_t index, struct nexus_vfs_dirent *entry);
bool vfs_open_readonly(const char *name, struct nexus_vfs_file *file);
bool vfs_file_size(const struct nexus_vfs_file *file, uint32_t *size);
bool vfs_read(struct nexus_vfs_file *file, void *buffer,
              size_t requested, size_t *bytes_read);
void vfs_close(struct nexus_vfs_file *file);

#endif
