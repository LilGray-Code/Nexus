#include "vfs.h"

#include "fat32.h"

#include <stddef.h>
#include <stdint.h>

static bool root_mounted;

static char ascii_upper(char value) {
    if (value >= 'a' && value <= 'z')
        return (char)(value - 'a' + 'A');
    return value;
}

static bool name_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL) return false;
    size_t i = 0;
    while (i < NEXUS_VFS_NAME_MAX) {
        const char a = ascii_upper(left[i]);
        const char b = ascii_upper(right[i]);
        if (a != b) return false;
        if (a == '\0') return true;
        ++i;
    }
    return false;
}

bool vfs_mount_root_fat32(const struct nexus_block_device *device,
                          const struct nexus_gpt_partition *partition) {
    root_mounted = false;
    if (!fat32_mount(device, partition)) return false;
    root_mounted = true;
    return true;
}

bool vfs_root_mounted(void) {
    return root_mounted;
}

size_t vfs_root_entry_count(void) {
    return root_mounted ? fat32_root_entry_count() : 0U;
}

bool vfs_root_entry(size_t index, struct nexus_vfs_dirent *entry) {
    if (!root_mounted || entry == NULL) return false;
    const struct nexus_fat32_entry *fat_entry = fat32_root_entry_at(index);
    if (fat_entry == NULL) return false;

    size_t i = 0;
    for (; i < NEXUS_VFS_NAME_MAX - 1U && fat_entry->name[i] != '\0'; ++i)
        entry->name[i] = fat_entry->name[i];
    entry->name[i] = '\0';
    entry->type = (fat_entry->attributes & 0x10U) != 0U
        ? NEXUS_VFS_DIRECTORY : NEXUS_VFS_FILE;
    entry->size = fat_entry->size;
    return true;
}

bool vfs_open_readonly(const char *name, struct nexus_vfs_file *file) {
    if (!root_mounted || name == NULL || file == NULL) return false;
    *file = (struct nexus_vfs_file){0};
    for (size_t i = 0; i < fat32_root_entry_count(); ++i) {
        const struct nexus_fat32_entry *entry = fat32_root_entry_at(i);
        if (entry == NULL || !name_equal(entry->name, name) ||
            (entry->attributes & 0x10U) != 0U) continue;
        file->filesystem_entry = entry;
        file->offset = 0;
        file->open = true;
        return true;
    }
    return false;
}

bool vfs_read(struct nexus_vfs_file *file, void *buffer,
              size_t requested, size_t *bytes_read) {
    if (bytes_read != NULL) *bytes_read = 0;
    if (!root_mounted || file == NULL || !file->open ||
        (requested != 0U && buffer == NULL) || bytes_read == NULL) {
        return false;
    }

    const struct nexus_fat32_entry *entry =
        (const struct nexus_fat32_entry *)file->filesystem_entry;
    if (entry == NULL || file->offset > entry->size) return false;
    size_t completed = 0;
    uint8_t *out = (uint8_t *)buffer;
    while (completed < requested && file->offset < entry->size) {
        size_t amount = requested - completed;
        if (amount > NEXUS_VFS_READ_CHUNK) amount = NEXUS_VFS_READ_CHUNK;
        const uint32_t remaining = entry->size - file->offset;
        if (amount > remaining) amount = remaining;
        if (!fat32_read_file(entry, file->offset, (uint32_t)amount,
                             out + completed)) {
            *bytes_read = completed;
            return false;
        }
        file->offset += (uint32_t)amount;
        completed += amount;
    }
    *bytes_read = completed;
    return true;
}

void vfs_close(struct nexus_vfs_file *file) {
    if (file == NULL) return;
    *file = (struct nexus_vfs_file){0};
}
