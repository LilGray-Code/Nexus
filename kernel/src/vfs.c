#include "vfs.h"

#include "fat32.h"
#include "blockdev.h"
#include "nxfs.h"

#include <stddef.h>
#include <stdint.h>

static bool root_mounted;
static bool nxfs_root_mounted;
#define VFS_DIRECTORY_MAX_DEPTH 16U
static uint32_t directory_stack[VFS_DIRECTORY_MAX_DEPTH];
static char directory_names[VFS_DIRECTORY_MAX_DEPTH][NEXUS_VFS_NAME_MAX];
static size_t directory_depth;

struct ramfs_entry {
    bool used;
    char name[NEXUS_VFS_NAME_MAX];
    uint32_t size;
    uint8_t data[NEXUS_VFS_RAM_MAX_FILE_SIZE];
};

static struct ramfs_entry ramfs[NEXUS_VFS_RAM_MAX_FILES];

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


static bool normalize_ramfs_name(const char *source,
                                 char output[NEXUS_VFS_NAME_MAX]) {
    if (source == NULL || output == NULL) return false;
    size_t length = 0U;
    bool extension = false;

    while (source[length] != '\0') {
        if (length >= NEXUS_VFS_NAME_MAX - 1U) return false;
        const char ch = source[length];
        if (ch == '.') {
            if (extension || length == 0U) return false;
            extension = true;
            output[length++] = '.';
            continue;
        }
        const bool valid = (ch >= 'a' && ch <= 'z') ||
                           (ch >= 'A' && ch <= 'Z') ||
                           (ch >= '0' && ch <= '9') ||
                           ch == '_' || ch == '-';
        if (!valid) return false;
        output[length++] = ascii_upper(ch);
    }

    if (length == 0U || output[length - 1U] == '.') return false;
    output[length] = '\0';
    return true;
}

static struct ramfs_entry *ramfs_find(const char *name) {
    char normalized[NEXUS_VFS_NAME_MAX];
    if (!normalize_ramfs_name(name, normalized)) return NULL;
    for (size_t i = 0; i < NEXUS_VFS_RAM_MAX_FILES; ++i) {
        if (ramfs[i].used && name_equal(ramfs[i].name, normalized))
            return &ramfs[i];
    }
    return NULL;
}

bool vfs_has_filesystem(void) {
    if (root_mounted || nxfs_root_mounted) return true;
    for (size_t i = 0; i < NEXUS_VFS_RAM_MAX_FILES; ++i) {
        if (ramfs[i].used) return true;
    }
    return false;
}

bool vfs_ramfs_create(const char *name) {
    char normalized[NEXUS_VFS_NAME_MAX];
    if (!normalize_ramfs_name(name, normalized) ||
        ramfs_find(normalized) != NULL) return false;

    struct ramfs_entry *slot = NULL;
    for (size_t i = 0; i < NEXUS_VFS_RAM_MAX_FILES; ++i) {
        if (!ramfs[i].used) {
            slot = &ramfs[i];
            break;
        }
    }
    if (slot == NULL) return false;

    *slot = (struct ramfs_entry){0};
    slot->used = true;
    for (size_t i = 0; i < NEXUS_VFS_NAME_MAX; ++i) {
        slot->name[i] = normalized[i];
        if (normalized[i] == '\0') break;
    }
    return true;
}

bool vfs_ramfs_write(const char *name, const void *data, size_t length) {
    if (length > NEXUS_VFS_RAM_MAX_FILE_SIZE ||
        (length != 0U && data == NULL)) return false;

    struct ramfs_entry *entry = ramfs_find(name);
    if (entry == NULL) {
        if (!vfs_ramfs_create(name)) return false;
        entry = ramfs_find(name);
    }
    if (entry == NULL) return false;

    const uint8_t *source = (const uint8_t *)data;
    for (size_t i = 0; i < length; ++i) entry->data[i] = source[i];
    for (size_t i = length; i < entry->size; ++i) entry->data[i] = 0U;
    entry->size = (uint32_t)length;
    return true;
}

bool vfs_ramfs_remove(const char *name) {
    struct ramfs_entry *entry = ramfs_find(name);
    if (entry == NULL) return false;
    for (size_t i = 0; i < NEXUS_VFS_RAM_MAX_FILE_SIZE; ++i)
        entry->data[i] = 0U;
    *entry = (struct ramfs_entry){0};
    return true;
}

/*
 * Remove only from writable Nexus-owned storage. FAT32 is deliberately
 * read-only, so a name found only on FAT32 is never deleted here.
 */
bool vfs_remove_file(const char *name) {
    if (name == NULL || directory_depth != 0U) return false;

    /* A volatile overlay shadows a persistent file with the same name. */
    if (ramfs_find(name) != NULL) return vfs_ramfs_remove(name);

    if (!nxfs_root_mounted || !nxfs_is_writable()) return false;
    for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
        const struct nexus_nxfs_entry *entry = nxfs_entry_at(i);
        if (entry != NULL && name_equal(entry->name, name))
            return nxfs_remove_file(entry->name);
    }
    return false;
}

bool vfs_mount_root_fat32(const struct nexus_block_device *device,
                          const struct nexus_gpt_partition *partition) {
    root_mounted = false;
    directory_depth = 0U;
    if (!fat32_mount(device, partition)) return false;
    if (!blockdev_set_mounted(device, true)) return false;
    root_mounted = true;
    directory_stack[0] = fat32_root_cluster();
    for (size_t i = 0U; i < VFS_DIRECTORY_MAX_DEPTH; ++i)
        directory_names[i][0] = '\0';
    return true;
}

bool vfs_root_mounted(void) {
    return root_mounted;
}

size_t vfs_directory_depth(void) {
    return directory_depth;
}

bool vfs_current_path(char *buffer, size_t capacity) {
    if (buffer == NULL || capacity < 2U) return false;
    size_t used = 0U;
    buffer[used++] = '/';
    for (size_t depth = 0U; depth < directory_depth; ++depth) {
        size_t length = 0U;
        while (length < NEXUS_VFS_NAME_MAX &&
               directory_names[depth][length] != '\0') ++length;
        if (length == 0U || length == NEXUS_VFS_NAME_MAX) return false;
        const size_t separator = used > 1U ? 1U : 0U;
        if (used + separator + length + 1U > capacity) return false;
        if (separator != 0U) buffer[used++] = '/';
        for (size_t i = 0U; i < length; ++i)
            buffer[used++] = directory_names[depth][i];
    }
    buffer[used] = '\0';
    return true;
}

bool vfs_enter_directory(const char *name) {
    if (!root_mounted || name == NULL ||
        directory_depth + 1U >= VFS_DIRECTORY_MAX_DEPTH) return false;
    for (size_t i = 0U; i < fat32_root_entry_count(); ++i) {
        const struct nexus_fat32_entry *entry = fat32_root_entry_at(i);
        if (entry == NULL || !name_equal(entry->name, name) ||
            (entry->attributes & 0x10U) == 0U ||
            entry->first_cluster < 2U) continue;
        const uint32_t child_cluster = entry->first_cluster;
        for (size_t depth = 0U; depth <= directory_depth; ++depth)
            if (directory_stack[depth] == child_cluster) return false;
        char selected_name[NEXUS_VFS_NAME_MAX];
        size_t length = 0U;
        while (length < NEXUS_VFS_NAME_MAX - 1U &&
               entry->name[length] != '\0') {
            selected_name[length] = entry->name[length];
            ++length;
        }
        selected_name[length] = '\0';
        if (!fat32_read_directory(child_cluster)) return false;
        directory_stack[++directory_depth] = child_cluster;
        for (size_t i = 0U; i <= length; ++i)
            directory_names[directory_depth - 1U][i] = selected_name[i];
        return true;
    }
    return false;
}

bool vfs_parent_directory(void) {
    if (!root_mounted || directory_depth == 0U) return false;
    const uint32_t parent = directory_stack[directory_depth - 1U];
    if (!fat32_read_directory(parent)) return false;
    directory_names[directory_depth - 1U][0] = '\0';
    --directory_depth;
    return true;
}

bool vfs_mount_root_nxfs(const struct nexus_block_device *device,
                         const struct nexus_gpt_partition *partition) {
    nxfs_root_mounted = false;
    if (!nxfs_mount(device, partition)) return false;
    if (!blockdev_set_mounted(device, true)) return false;
    nxfs_root_mounted = true;
    return true;
}

bool vfs_nxfs_mounted(void) {
    return nxfs_root_mounted;
}

bool vfs_nxfs_writable(void) {
    return nxfs_root_mounted && nxfs_is_writable();
}

bool vfs_save_ram_file_to_nxfs(const char *name) {
    if (directory_depth != 0U || !nxfs_root_mounted || !nxfs_is_writable()) return false;
    const struct ramfs_entry *entry = ramfs_find(name);
    if (entry == NULL || entry->size > NEXUS_NXFS_MAX_FILE_BYTES)
        return false;
    return nxfs_write_file(name, entry->data, entry->size);
}

/*
 * User-facing writes prefer the explicitly mounted persistent NexusFS volume.
 * If it is absent or read-only, writes go to the volatile RAM overlay. Never
 * fall through to FAT32 and never disguise a failed writable-NXFS write as a
 * successful RAM-only write.
 */
bool vfs_write_file(const char *name, const void *data, size_t length) {
    if (directory_depth != 0U || name == NULL || (length != 0U && data == NULL)) return false;
    if (nxfs_root_mounted && nxfs_is_writable()) {
        if (length > NEXUS_NXFS_MAX_FILE_BYTES) return false;
        return nxfs_write_file(name, data, length);
    }
    return vfs_ramfs_write(name, data, length);
}

size_t vfs_root_entry_count(void) {
    if (directory_depth != 0U)
        return root_mounted ? fat32_root_entry_count() : 0U;
    size_t count = root_mounted ? fat32_root_entry_count() : 0U;
    if (nxfs_root_mounted) count += nxfs_entry_count();
    for (size_t i = 0U; i < NEXUS_VFS_RAM_MAX_FILES; ++i)
        if (ramfs[i].used) ++count;
    return count;
}

bool vfs_root_entry(size_t index, struct nexus_vfs_dirent *entry) {
    if (entry == NULL) return false;
    if (directory_depth != 0U) {
        const struct nexus_fat32_entry *fat = fat32_root_entry_at(index);
        if (fat == NULL) return false;
        size_t n = 0U;
        while (n < NEXUS_VFS_NAME_MAX - 1U && fat->name[n] != '\0') {
            entry->name[n] = fat->name[n];
            ++n;
        }
        entry->name[n] = '\0';
        entry->type = (fat->attributes & 0x10U) != 0U
            ? NEXUS_VFS_DIRECTORY : NEXUS_VFS_FILE;
        entry->size = fat->size;
        entry->first_cluster = fat->first_cluster;
        return true;
    }
    for (size_t i = 0U; i < NEXUS_VFS_RAM_MAX_FILES; ++i) {
        if (!ramfs[i].used) continue;
        if (index == 0U) {
            size_t n = 0U;
            while (ramfs[i].name[n] != '\0' && n < NEXUS_VFS_NAME_MAX - 1U) {
                entry->name[n] = ramfs[i].name[n];
                ++n;
            }
            entry->name[n] = '\0';
            entry->type = NEXUS_VFS_FILE;
            entry->size = ramfs[i].size;
            entry->first_cluster = 0U;
            return true;
        }
        --index;
    }
    if (nxfs_root_mounted) {
        const size_t nxfs_count = nxfs_entry_count();
        if (index < nxfs_count) {
            const struct nexus_nxfs_entry *nx_entry = nxfs_entry_at(index);
            if (nx_entry == NULL) return false;
            size_t n = 0U;
            while (n < NEXUS_VFS_NAME_MAX - 1U &&
                   nx_entry->name[n] != '\0') {
                entry->name[n] = nx_entry->name[n];
                ++n;
            }
            entry->name[n] = '\0';
            entry->type = NEXUS_VFS_FILE;
            entry->size = nx_entry->size;
            entry->first_cluster = 0U;
            return true;
        }
        index -= nxfs_count;
    }
    if (!root_mounted) return false;
    const struct nexus_fat32_entry *fat = fat32_root_entry_at(index);
    if (fat == NULL) return false;
    size_t n = 0U;
    while (n < NEXUS_VFS_NAME_MAX - 1U && fat->name[n] != '\0') {
        entry->name[n] = fat->name[n];
        ++n;
    }
    entry->name[n] = '\0';
    entry->type = (fat->attributes & 0x10U) != 0U
        ? NEXUS_VFS_DIRECTORY : NEXUS_VFS_FILE;
    entry->size = fat->size;
    entry->first_cluster = fat->first_cluster;
    return true;
}

bool vfs_open_readonly(const char *name, struct nexus_vfs_file *file) {
    if (name == NULL || file == NULL) return false;
    *file = (struct nexus_vfs_file){0};
    if (directory_depth == 0U) {
        struct ramfs_entry *ram_entry = ramfs_find(name);
        if (ram_entry != NULL) {
            file->filesystem_entry = ram_entry;
            file->offset = 0U;
            file->open = true;
            file->ram_file = true;
            return true;
        }
        if (nxfs_root_mounted) {
            for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
                const struct nexus_nxfs_entry *entry = nxfs_entry_at(i);
                if (entry == NULL || !name_equal(entry->name, name)) continue;
                file->filesystem_entry = entry;
                file->offset = 0U;
                file->open = true;
                file->nxfs_file = true;
                return true;
            }
        }
    }
    if (!root_mounted) return false;
    for (size_t i = 0U; i < fat32_root_entry_count(); ++i) {
        const struct nexus_fat32_entry *entry = fat32_root_entry_at(i);
        if (entry == NULL || !name_equal(entry->name, name) ||
            (entry->attributes & 0x10U) != 0U) continue;
        file->fat32_snapshot = *entry;
        file->offset = 0U;
        file->open = true;
        file->fat32_file = true;
        return true;
    }
    return false;
}

bool vfs_file_writable(const char *name) {
    if (name == NULL || directory_depth != 0U) return false;
    if (ramfs_find(name) != NULL) return true;
    if (!nxfs_root_mounted || !nxfs_is_writable()) return false;
    for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
        const struct nexus_nxfs_entry *entry = nxfs_entry_at(i);
        if (entry != NULL && name_equal(entry->name, name)) return true;
    }
    return false;
}

bool vfs_file_size(const struct nexus_vfs_file *file, uint32_t *size) {
    if (file == NULL || !file->open || size == NULL) return false;
    if (file->fat32_file) {
        if (!root_mounted) return false;
        *size = file->fat32_snapshot.size;
        return true;
    }
    if (file->filesystem_entry == NULL) return false;
    if (file->ram_file) {
        const struct ramfs_entry *entry =
            (const struct ramfs_entry *)file->filesystem_entry;
        if (!entry->used) return false;
        *size = entry->size;
        return true;
    }
    if (file->nxfs_file) {
        const struct nexus_nxfs_entry *entry =
            (const struct nexus_nxfs_entry *)file->filesystem_entry;
        if (!nxfs_root_mounted || entry == NULL || !entry->used) return false;
        *size = entry->size;
        return true;
    }
    return false;
}

bool vfs_read(struct nexus_vfs_file *file, void *buffer,
              size_t requested, size_t *bytes_read) {
    if (bytes_read != NULL) *bytes_read = 0U;
    if (file == NULL || !file->open ||
        (requested != 0U && buffer == NULL) || bytes_read == NULL) return false;
    if (file->fat32_file) {
        if (!root_mounted || file->offset > file->fat32_snapshot.size)
            return false;
        size_t completed = 0U;
        uint8_t *out = (uint8_t *)buffer;
        while (completed < requested &&
               file->offset < file->fat32_snapshot.size) {
            size_t amount = requested - completed;
            if (amount > NEXUS_VFS_READ_CHUNK)
                amount = NEXUS_VFS_READ_CHUNK;
            const uint32_t remaining =
                file->fat32_snapshot.size - file->offset;
            if (amount > remaining) amount = remaining;
            if (!fat32_read_file(&file->fat32_snapshot, file->offset,
                                 (uint32_t)amount, out + completed)) {
                *bytes_read = completed;
                return false;
            }
            file->offset += (uint32_t)amount;
            completed += amount;
        }
        *bytes_read = completed;
        return true;
    }
    if (file->ram_file) {
        const struct ramfs_entry *ram_entry =
            (const struct ramfs_entry *)file->filesystem_entry;
        if (ram_entry == NULL || !ram_entry->used ||
            file->offset > ram_entry->size) return false;
        size_t amount = requested;
        const uint32_t remaining = ram_entry->size - file->offset;
        if (amount > remaining) amount = remaining;
        if (amount > NEXUS_VFS_READ_CHUNK) amount = NEXUS_VFS_READ_CHUNK;
        uint8_t *out = (uint8_t *)buffer;
        for (size_t i = 0U; i < amount; ++i)
            out[i] = ram_entry->data[file->offset + (uint32_t)i];
        file->offset += (uint32_t)amount;
        *bytes_read = amount;
        return true;
    }
    if (file->nxfs_file) {
        const struct nexus_nxfs_entry *entry =
            (const struct nexus_nxfs_entry *)file->filesystem_entry;
        if (!nxfs_root_mounted || entry == NULL || !entry->used ||
            file->offset > entry->size) return false;
        size_t amount = requested;
        const uint32_t remaining = entry->size - file->offset;
        if (amount > remaining) amount = remaining;
        if (amount > NEXUS_VFS_READ_CHUNK) amount = NEXUS_VFS_READ_CHUNK;
        if (!nxfs_read_file(entry, file->offset, (uint32_t)amount, buffer))
            return false;
        file->offset += (uint32_t)amount;
        *bytes_read = amount;
        return true;
    }
    return false;
}

void vfs_close(struct nexus_vfs_file *file) {
    if (file == NULL) return;
    *file = (struct nexus_vfs_file){0};
}


bool vfs_ramfs_selftest(void) {
    static const char payload[] = "NEXUS RAMFS READY";
    static const char long_name[] = "NEXUS-LONG-FILE-NAME-ROUNDTRIP";
    static const char long_payload[] = "long filenames remain readable";
    if (!vfs_write_file("NXRAM.TST", payload, sizeof(payload) - 1U))
        return false;

    struct nexus_vfs_file file;
    if (!vfs_open_readonly("nxram.tst", &file) ||
        (vfs_nxfs_writable() ? !file.nxfs_file : !file.ram_file))
        return false;
    uint8_t buffer[64];
    size_t bytes_read = 0U;
    const bool read_ok = vfs_read(&file, buffer, sizeof(buffer), &bytes_read);
    vfs_close(&file);
    if (!read_ok || bytes_read != sizeof(payload) - 1U) return false;
    for (size_t i = 0; i < bytes_read; ++i) {
        if (buffer[i] != (uint8_t)payload[i]) return false;
    }
    if (!vfs_remove_file("NXRAM.TST")) return false;
    if (vfs_open_readonly("NXRAM.TST", &file)) {
        vfs_close(&file);
        return false;
    }

    /* Exercise names longer than the legacy 8.3 limit on both NXFS and RAMFS. */
    if (!vfs_write_file(long_name, long_payload, sizeof(long_payload) - 1U) ||
        !vfs_open_readonly("nexus-long-file-name-roundtrip", &file)) {
        (void)vfs_remove_file(long_name);
        return false;
    }
    bytes_read = 0U;
    const bool long_read_ok = vfs_read(&file, buffer, sizeof(buffer), &bytes_read);
    vfs_close(&file);
    if (!long_read_ok || bytes_read != sizeof(long_payload) - 1U) {
        (void)vfs_remove_file(long_name);
        return false;
    }
    for (size_t i = 0U; i < bytes_read; ++i) {
        if (buffer[i] != (uint8_t)long_payload[i]) {
            (void)vfs_remove_file(long_name);
            return false;
        }
    }
    return vfs_remove_file(long_name);
}
