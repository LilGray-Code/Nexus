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
/* A single bounded copy workspace avoids large kernel-stack allocations. */
static uint8_t vfs_copy_buffer[NEXUS_NXFS_MAX_FILE_BYTES];

static void vfs_copy_wipe(size_t length) {
    if (length > sizeof(vfs_copy_buffer)) length = sizeof(vfs_copy_buffer);
    volatile uint8_t *buffer = vfs_copy_buffer;
    for (size_t i = 0U; i < length; ++i) buffer[i] = 0U;
}

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
    if (!fat32_mount(device, partition)) return false;
    if (!blockdev_set_mounted(device, true)) return false;
    root_mounted = true;
    directory_depth = 0U;
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
    if (buffer == NULL || capacity < 2U || !root_mounted) return false;
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
        char selected_name[NEXUS_VFS_NAME_MAX];
        size_t name_length = 0U;
        while (name_length < NEXUS_VFS_NAME_MAX - 1U &&
               entry->name[name_length] != '\0') {
            selected_name[name_length] = entry->name[name_length];
            ++name_length;
        }
        selected_name[name_length] = '\0';
        for (size_t depth = 0U; depth <= directory_depth; ++depth) {
            if (directory_stack[depth] == child_cluster) return false;
        }
        /* fat32_read_directory replaces the active entry table, so preserve
         * the selected name before loading the child directory. */
        if (!fat32_read_directory(child_cluster)) return false;
        directory_stack[++directory_depth] = child_cluster;
        for (size_t i = 0U; i <= name_length; ++i)
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
    if (!nxfs_root_mounted || !nxfs_is_writable()) return false;
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
    if (name == NULL || directory_depth != 0U ||
        (length != 0U && data == NULL)) return false;
    if (nxfs_root_mounted && nxfs_is_writable()) {
        if (length > NEXUS_NXFS_MAX_FILE_BYTES) return false;
        return nxfs_write_file(name, data, length);
    }
    return vfs_ramfs_write(name, data, length);
}

bool vfs_copy_file(const char *source_name,
                    const char *destination_name) {
    if (source_name == NULL || destination_name == NULL ||
        source_name[0] == '\0' || destination_name[0] == '\0')
        return false;

    /* Never silently replace an existing Nexus-owned destination. A file
     * copied out of a nested read-only folder may keep its original name. */
    if (ramfs_find(destination_name) != NULL) return false;
    if (nxfs_root_mounted) {
        for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
            const struct nexus_nxfs_entry *entry = nxfs_entry_at(i);
            if (entry != NULL && name_equal(entry->name, destination_name))
                return false;
        }
    }

    struct nexus_vfs_file source;
    if (!vfs_open_readonly(source_name, &source)) return false;

    uint32_t size = 0U;
    if (!vfs_file_size(&source, &size) ||
        size > NEXUS_NXFS_MAX_FILE_BYTES) {
        vfs_close(&source);
        return false;
    }

    size_t copied = 0U;
    bool read_ok = true;
    while (copied < size) {
        size_t amount = (size_t)size - copied;
        if (amount > NEXUS_VFS_READ_CHUNK) amount = NEXUS_VFS_READ_CHUNK;
        size_t received = 0U;
        if (!vfs_read(&source, vfs_copy_buffer + copied,
                      amount, &received) || received == 0U) {
            read_ok = false;
            break;
        }
        copied += received;
    }
    vfs_close(&source);
    if (!read_ok || copied != size) {
        vfs_copy_wipe(copied);
        return false;
    }

    /* Copying from a nested FAT32 folder writes only to the Nexus root
     * volume. FAT32 remains read-only and never receives copy writes. */
    bool write_ok = false;
    if (nxfs_root_mounted && nxfs_is_writable()) {
        write_ok = nxfs_write_file(destination_name, vfs_copy_buffer, size);
    } else if (size <= NEXUS_VFS_RAM_MAX_FILE_SIZE) {
        write_ok = vfs_ramfs_write(destination_name, vfs_copy_buffer, size);
    }
    vfs_copy_wipe(size);
    return write_ok;
}

/*
 * Move is restricted to Nexus-owned writable files. It copies and verifies
 * the destination through the normal VFS path before deleting the source.
 * Read-only FAT32 entries can be copied out, but never moved or deleted.
 */
bool vfs_move_file(const char *source_name,
                   const char *destination_name) {
    if (source_name == NULL || destination_name == NULL ||
        source_name[0] == '\0' || destination_name[0] == '\0' ||
        name_equal(source_name, destination_name)) return false;

    struct nexus_vfs_file source;
    if (!vfs_open_readonly(source_name, &source)) return false;
    const bool writable_source = source.ram_file || source.nxfs_file;
    vfs_close(&source);
    if (!writable_source) return false;

    if (!vfs_copy_file(source_name, destination_name)) return false;
    if (vfs_remove_file(source_name)) return true;

    /* Preserve the source if deletion fails; best-effort rollback the copy. */
    (void)vfs_remove_file(destination_name);
    return false;
}

/* Higher-priority writable layers hide lower-layer names, matching open(). */
static bool ramfs_shadows_name(const char *name) {
    return ramfs_find(name) != NULL;
}

static bool nxfs_shadows_name(const char *name) {
    if (!nxfs_root_mounted || name == NULL) return false;
    for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
        const struct nexus_nxfs_entry *entry = nxfs_entry_at(i);
        if (entry != NULL && name_equal(entry->name, name)) return true;
    }
    return false;
}

size_t vfs_root_entry_count(void) {
    size_t count = 0U;
    if (directory_depth == 0U) for (size_t i = 0U; i < NEXUS_VFS_RAM_MAX_FILES; ++i) {
        if (ramfs[i].used) ++count;
    }
    if (directory_depth == 0U && nxfs_root_mounted) {
        for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
            const struct nexus_nxfs_entry *entry = nxfs_entry_at(i);
            if (entry != NULL && !ramfs_shadows_name(entry->name)) ++count;
        }
    }
    if (root_mounted) {
        for (size_t i = 0U; i < fat32_root_entry_count(); ++i) {
            const struct nexus_fat32_entry *entry = fat32_root_entry_at(i);
            if (entry == NULL ||
                (directory_depth == 0U &&
                 (ramfs_shadows_name(entry->name) ||
                  nxfs_shadows_name(entry->name)))) continue;
            ++count;
        }
    }
    return count;
}

bool vfs_root_entry(size_t index, struct nexus_vfs_dirent *entry) {
    if (entry == NULL) return false;
    if (directory_depth == 0U) for (size_t i = 0U; i < NEXUS_VFS_RAM_MAX_FILES; ++i) {
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

    if (directory_depth == 0U && nxfs_root_mounted) {
        for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
            const struct nexus_nxfs_entry *nx_entry = nxfs_entry_at(i);
            if (nx_entry == NULL || ramfs_shadows_name(nx_entry->name)) continue;
            if (index != 0U) {
                --index;
                continue;
            }
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
    }

    if (root_mounted) {
        for (size_t i = 0U; i < fat32_root_entry_count(); ++i) {
            const struct nexus_fat32_entry *fat_entry = fat32_root_entry_at(i);
            if (fat_entry == NULL ||
                (directory_depth == 0U &&
                 (ramfs_shadows_name(fat_entry->name) ||
                  nxfs_shadows_name(fat_entry->name)))) continue;
            if (index != 0U) {
                --index;
                continue;
            }
            size_t n = 0U;
            for (; n < sizeof(fat_entry->name) - 1U &&
                   fat_entry->name[n] != '\0'; ++n) {
                entry->name[n] = fat_entry->name[n];
            }
            entry->name[n] = '\0';
            entry->type = (fat_entry->attributes & 0x10U) != 0U
                ? NEXUS_VFS_DIRECTORY : NEXUS_VFS_FILE;
            entry->size = fat_entry->size;
            entry->first_cluster = fat_entry->first_cluster;
            return true;
        }
    }
    return false;
}

bool vfs_open_readonly(const char *name, struct nexus_vfs_file *file) {
    if (name == NULL || file == NULL) return false;
    *file = (struct nexus_vfs_file){0};

    struct ramfs_entry *ram_entry = directory_depth == 0U
        ? ramfs_find(name) : NULL;
    if (ram_entry != NULL) {
        file->filesystem_entry = ram_entry;
        file->offset = 0U;
        file->open = true;
        file->ram_file = true;
        return true;
    }

    if (directory_depth == 0U && nxfs_root_mounted) {
        for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
            const struct nexus_nxfs_entry *entry = nxfs_entry_at(i);
            if (entry == NULL || !name_equal(entry->name, name)) continue;
            file->filesystem_entry = entry;
            file->nxfs_snapshot = *entry;
            file->nxfs_snapshot_valid = true;
            file->offset = 0U;
            file->open = true;
            file->nxfs_file = true;
            return true;
        }
    }

    if (!root_mounted) return false;
    for (size_t i = 0; i < fat32_root_entry_count(); ++i) {
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

bool vfs_file_size(const struct nexus_vfs_file *file, uint32_t *size) {
    if (file == NULL || !file->open || size == NULL ||
        (!file->ram_file && !file->nxfs_file && !file->fat32_file) ||
        (file->ram_file && file->filesystem_entry == NULL) ||
        (file->nxfs_file && !file->nxfs_snapshot_valid)) return false;
    if (file->ram_file) {
        const struct ramfs_entry *entry =
            (const struct ramfs_entry *)file->filesystem_entry;
        if (!entry->used) return false;
        *size = entry->size;
        return true;
    }
    if (file->nxfs_file) {
        const struct nexus_nxfs_entry *entry = &file->nxfs_snapshot;
        if (!nxfs_root_mounted || !file->nxfs_snapshot_valid ||
            !nxfs_entry_is_current(entry)) return false;
        *size = entry->size;
        return true;
    }
    if (!root_mounted || !file->fat32_file) return false;
    *size = file->fat32_snapshot.size;
    return true;
}

bool vfs_read(struct nexus_vfs_file *file, void *buffer,
              size_t requested, size_t *bytes_read) {
    if (bytes_read != NULL) *bytes_read = 0;
    if (file == NULL || !file->open ||
        (requested != 0U && buffer == NULL) || bytes_read == NULL) return false;

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
        for (size_t i = 0; i < amount; ++i)
            out[i] = ram_entry->data[file->offset + (uint32_t)i];
        file->offset += (uint32_t)amount;
        *bytes_read = amount;
        return true;
    }

    if (file->nxfs_file) {
        const struct nexus_nxfs_entry *entry = &file->nxfs_snapshot;
        if (!nxfs_root_mounted || !file->nxfs_snapshot_valid ||
            !entry->used || file->offset > entry->size) return false;
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

    if (!root_mounted || !file->fat32_file) return false;
    const struct nexus_fat32_entry *entry = &file->fat32_snapshot;
    if (file->offset > entry->size) return false;
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
    if (!vfs_copy_file("NXRAM.TST", "NXRAM-COPY.TST") ||
        !vfs_open_readonly("NXRAM-COPY.TST", &file)) {
        (void)vfs_remove_file("NXRAM.TST");
        return false;
    }
    bytes_read = 0U;
    const bool copy_read_ok = vfs_read(&file, buffer, sizeof(buffer),
                                       &bytes_read);
    vfs_close(&file);
    if (!copy_read_ok || bytes_read != sizeof(payload) - 1U) {
        (void)vfs_remove_file("NXRAM-COPY.TST");
        (void)vfs_remove_file("NXRAM.TST");
        return false;
    }
    for (size_t i = 0U; i < bytes_read; ++i) {
        if (buffer[i] != (uint8_t)payload[i]) {
            (void)vfs_remove_file("NXRAM-COPY.TST");
            (void)vfs_remove_file("NXRAM.TST");
            return false;
        }
    }
    if (!vfs_remove_file("NXRAM-COPY.TST") ||
        !vfs_remove_file("NXRAM.TST")) return false;
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
    if (!vfs_remove_file(long_name)) return false;

    /*
     * Exercise multi-chunk reads on persistent files. vfs_read deliberately
     * bounds each transfer to NEXUS_VFS_READ_CHUNK, so callers handling larger
     * files must advance until the complete file has been consumed.
     */
    if (vfs_nxfs_writable()) {
        static const char large_name[] = "NXFS-LARGE-READ-TEST.BIN";
        static const char large_copy_name[] = "NXFS-LARGE-COPY-TEST.BIN";
        static uint8_t large_payload[2U * NEXUS_VFS_READ_CHUNK + 137U];
        uint8_t chunk_buffer[NEXUS_VFS_READ_CHUNK];
        for (size_t i = 0U; i < sizeof(large_payload); ++i)
            large_payload[i] = (uint8_t)((i * 37U + 11U) & 0xFFU);
        if (!vfs_write_file(large_name, large_payload, sizeof(large_payload)) ||
            !vfs_open_readonly(large_name, &file)) {
            (void)vfs_remove_file(large_name);
            return false;
        }
        size_t total_read = 0U;
        bool large_read_ok = true;
        while (total_read < sizeof(large_payload)) {
            size_t received = 0U;
            if (!vfs_read(&file, chunk_buffer, sizeof(chunk_buffer), &received) ||
                received == 0U) {
                large_read_ok = false;
                break;
            }
            for (size_t i = 0U; i < received; ++i) {
                if (chunk_buffer[i] != large_payload[total_read + i]) {
                    large_read_ok = false;
                    break;
                }
            }
            if (!large_read_ok) break;
            total_read += received;
        }
        vfs_close(&file);
        if (!large_read_ok || total_read != sizeof(large_payload))
            return false;

        /* The copy operation must stream multi-chunk sources and refuse to
         * overwrite an existing destination on a later duplicate request. */
        if (!vfs_copy_file(large_name, large_copy_name) ||
            vfs_copy_file(large_name, large_copy_name) ||
            !vfs_open_readonly(large_copy_name, &file)) {
            (void)vfs_remove_file(large_copy_name);
            (void)vfs_remove_file(large_name);
            return false;
        }
        total_read = 0U;
        large_read_ok = true;
        while (total_read < sizeof(large_payload)) {
            size_t received = 0U;
            if (!vfs_read(&file, chunk_buffer, sizeof(chunk_buffer),
                          &received) || received == 0U) {
                large_read_ok = false;
                break;
            }
            for (size_t i = 0U; i < received; ++i) {
                if (chunk_buffer[i] !=
                    large_payload[total_read + i]) {
                    large_read_ok = false;
                    break;
                }
            }
            if (!large_read_ok) break;
            total_read += received;
        }
        vfs_close(&file);
        const bool copy_removed = vfs_remove_file(large_copy_name);
        const bool source_removed = vfs_remove_file(large_name);
        if (!large_read_ok || total_read != sizeof(large_payload) ||
            !copy_removed || !source_removed)
            return false;
    }

    /*
     * A volatile RAMFS overlay must hide, not duplicate, a same-named NXFS
     * file in directory listings. Removing the overlay reveals the persistent
     * file again without deleting it.
     */
    if (vfs_nxfs_writable()) {
        static const char shadow_name[] = "NXFS-SHADOW-TEST.TST";
        static const char persistent_data[] = "persistent";
        static const char overlay_data[] = "volatile";
        const size_t before = vfs_root_entry_count();
        if (!vfs_write_file(shadow_name, persistent_data,
                            sizeof(persistent_data) - 1U))
            return false;
        if (vfs_root_entry_count() != before + 1U ||
            !vfs_ramfs_write(shadow_name, overlay_data,
                             sizeof(overlay_data) - 1U)) {
            (void)vfs_remove_file(shadow_name);
            return false;
        }
        if (vfs_root_entry_count() != before + 1U) {
            (void)vfs_ramfs_remove(shadow_name);
            (void)vfs_remove_file(shadow_name);
            return false;
        }

        size_t visible_matches = 0U;
        for (size_t i = 0U; i < vfs_root_entry_count(); ++i) {
            struct nexus_vfs_dirent listed;
            if (vfs_root_entry(i, &listed) &&
                name_equal(listed.name, shadow_name))
                ++visible_matches;
        }
        if (visible_matches != 1U ||
            !vfs_open_readonly(shadow_name, &file) || !file.ram_file) {
            (void)vfs_ramfs_remove(shadow_name);
            (void)vfs_remove_file(shadow_name);
            return false;
        }
        vfs_close(&file);
        if (!vfs_ramfs_remove(shadow_name) ||
            vfs_root_entry_count() != before + 1U ||
            !vfs_open_readonly(shadow_name, &file) || !file.nxfs_file) {
            (void)vfs_remove_file(shadow_name);
            return false;
        }
        vfs_close(&file);
        if (!vfs_remove_file(shadow_name)) return false;
    }

    /*
     * An open persistent handle must not silently switch to a replacement
     * generation when another operation overwrites the same pathname.
     */
    if (vfs_nxfs_writable()) {
        static const char snapshot_name[] = "NXFS-HANDLE-SNAPSHOT.TST";
        static const char first_data[] = "first generation";
        static const char second_data[] = "second generation";
        struct nexus_vfs_file snapshot_file;
        uint8_t snapshot_buffer[32];
        size_t snapshot_read = 0U;
        if (!vfs_write_file(snapshot_name, first_data,
                            sizeof(first_data) - 1U) ||
            !vfs_open_readonly(snapshot_name, &snapshot_file) ||
            !snapshot_file.nxfs_file ||
            !vfs_write_file(snapshot_name, second_data,
                            sizeof(second_data) - 1U)) {
            (void)vfs_remove_file(snapshot_name);
            return false;
        }
        uint32_t stale_size = 0U;
        const bool stale_read_rejected =
            !vfs_file_size(&snapshot_file, &stale_size) &&
            !vfs_read(&snapshot_file, snapshot_buffer,
                      sizeof(snapshot_buffer), &snapshot_read) &&
            snapshot_read == 0U;
        vfs_close(&snapshot_file);
        if (!stale_read_rejected || !vfs_remove_file(snapshot_name))
            return false;
    }
    return true;
}
