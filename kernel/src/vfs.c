#include "vfs.h"

#include "fat32.h"
#include "blockdev.h"
#include "nxfs.h"
#include "boot_pack.h"

#include <stddef.h>
#include <stdint.h>

static bool root_mounted;
static bool nxfs_root_mounted;
enum vfs_directory_backend {
    VFS_DIRECTORY_ROOT = 0,
    VFS_DIRECTORY_FAT32,
    VFS_DIRECTORY_NXFS
};
static enum vfs_directory_backend directory_backend;
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
static bool vfs_stream_active;

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

static bool vfs_same_prefix(const char *value, const char *prefix,
                            size_t length) {
    if (value == NULL || prefix == NULL) return false;
    for (size_t i = 0U; i < length; ++i) {
        if (value[i] == '\0' ||
            ascii_upper(value[i]) != ascii_upper(prefix[i])) return false;
    }
    return true;
}

static bool vfs_nxfs_current_path(char output[NEXUS_VFS_NAME_MAX]) {
    if (output == NULL || directory_backend == VFS_DIRECTORY_FAT32)
        return false;
    size_t used = 0U;
    output[0] = '\0';
    for (size_t depth = 0U; depth < directory_depth; ++depth) {
        size_t length = 0U;
        while (length < NEXUS_VFS_NAME_MAX &&
               directory_names[depth][length] != '\0') ++length;
        if (length == 0U || length >= NEXUS_VFS_NAME_MAX ||
            used + (used != 0U ? 1U : 0U) + length >=
                NEXUS_VFS_NAME_MAX) return false;
        if (used != 0U) output[used++] = '/';
        for (size_t i = 0U; i < length; ++i)
            output[used++] = ascii_upper(directory_names[depth][i]);
    }
    output[used] = '\0';
    return true;
}

static bool vfs_nxfs_compose_path(const char *leaf,
                                  char output[NEXUS_VFS_NAME_MAX]) {
    if (leaf == NULL || output == NULL ||
        directory_backend == VFS_DIRECTORY_FAT32) return false;
    size_t used = 0U;
    if (!vfs_nxfs_current_path(output)) return false;
    while (used < NEXUS_VFS_NAME_MAX && output[used] != '\0') ++used;
    if (leaf[0] == '\0') return false;
    if (used != 0U) {
        if (used + 1U >= NEXUS_VFS_NAME_MAX) return false;
        output[used++] = '/';
    }
    size_t i = 0U;
    while (leaf[i] != '\0') {
        if (used >= NEXUS_VFS_NAME_MAX - 1U || leaf[i] == '/') return false;
        output[used++] = ascii_upper(leaf[i]);
        ++i;
    }
    if (i == 0U) return false;
    output[used] = '\0';
    return true;
}

static const struct nexus_nxfs_entry *vfs_nxfs_find_path(
    const char *path) {
    if (!nxfs_root_mounted || path == NULL) return NULL;
    for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
        const struct nexus_nxfs_entry *entry = nxfs_entry_at(i);
        if (entry != NULL && name_equal(entry->name, path)) return entry;
    }
    return NULL;
}

static bool vfs_nxfs_direct_child(
    const struct nexus_nxfs_entry *entry, const char *parent,
    char leaf[NEXUS_VFS_NAME_MAX]) {
    if (entry == NULL || !entry->used || parent == NULL || leaf == NULL)
        return false;
    size_t parent_length = 0U;
    while (parent_length < NEXUS_VFS_NAME_MAX &&
           parent[parent_length] != '\0') ++parent_length;
    if (parent_length >= NEXUS_VFS_NAME_MAX) return false;
    size_t offset = 0U;
    if (parent_length != 0U) {
        if (!vfs_same_prefix(entry->name, parent, parent_length) ||
            entry->name[parent_length] != '/') return false;
        offset = parent_length + 1U;
    }
    size_t length = 0U;
    while (entry->name[offset + length] != '\0' &&
           entry->name[offset + length] != '/') {
        if (length >= NEXUS_VFS_NAME_MAX - 1U) return false;
        leaf[length] = entry->name[offset + length];
        ++length;
    }
    if (length == 0U || entry->name[offset + length] == '/') return false;
    leaf[length] = '\0';
    return true;
}

static size_t vfs_nxfs_child_count(const char *parent) {
    if (!nxfs_root_mounted || parent == NULL) return 0U;
    size_t count = 0U;
    for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
        char leaf[NEXUS_VFS_NAME_MAX];
        if (vfs_nxfs_direct_child(nxfs_entry_at(i), parent, leaf)) ++count;
    }
    return count;
}

static const struct nexus_nxfs_entry *vfs_nxfs_child_at(
    const char *parent, size_t index, char leaf_out[NEXUS_VFS_NAME_MAX]) {
    if (!nxfs_root_mounted || parent == NULL || leaf_out == NULL)
        return NULL;
    for (size_t i = 0U; i < nxfs_entry_count(); ++i) {
        const struct nexus_nxfs_entry *entry = nxfs_entry_at(i);
        char leaf[NEXUS_VFS_NAME_MAX];
        if (!vfs_nxfs_direct_child(entry, parent, leaf)) continue;
        if (index == 0U) {
            for (size_t k = 0U; k < NEXUS_VFS_NAME_MAX; ++k) {
                leaf_out[k] = leaf[k];
                if (leaf[k] == '\0') break;
            }
            return entry;
        }
        --index;
    }
    return NULL;
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
    if (root_mounted || nxfs_root_mounted || nexus_boot_pack_ready()) return true;
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
    if (name == NULL || directory_backend == VFS_DIRECTORY_FAT32) return false;
    if (directory_backend == VFS_DIRECTORY_ROOT &&
        ramfs_find(name) != NULL) return vfs_ramfs_remove(name);
    if (!nxfs_root_mounted || !nxfs_is_writable()) return false;
    char path[NEXUS_VFS_NAME_MAX];
    if (!vfs_nxfs_compose_path(name, path)) return false;
    return nxfs_remove_file(path);
}

static bool vfs_target_is_leaf(const char *target) {
    if (target == NULL || target[0] == '/') return false;
    size_t length = 0U;
    while (length < NEXUS_VFS_NAME_MAX && target[length] != '\0') {
        if (target[length] == '/') return false;
        ++length;
    }
    return length != 0U && length < NEXUS_VFS_NAME_MAX;
}

static bool vfs_nxfs_resolve_target_path(
    const char *target, char output[NEXUS_VFS_NAME_MAX]) {
    if (target == NULL || output == NULL ||
        directory_backend == VFS_DIRECTORY_FAT32 || target[0] == '\0')
        return false;

    const bool absolute = target[0] == '/';
    const size_t offset = absolute ? 1U : 0U;
    if (target[offset] == '\0') return false;
    bool has_separator = absolute;
    size_t length = 0U;
    while (length < NEXUS_VFS_NAME_MAX && target[offset + length] != '\0') {
        if (target[offset + length] == '/') has_separator = true;
        ++length;
    }
    if (length == 0U || length >= NEXUS_VFS_NAME_MAX) return false;
    if (!has_separator)
        return vfs_nxfs_compose_path(target + offset, output);

    for (size_t i = 0U; i < length; ++i) {
        char value = target[offset + i];
        if (value >= 'a' && value <= 'z')
            value = (char)(value - 'a' + 'A');
        output[i] = value;
    }
    output[length] = '\0';
    return true;
}

/* A move changes one persistent metadata record; no file-sized buffer needed. */
bool vfs_move_file(const char *name, const char *destination_path) {
    if (name == NULL || destination_path == NULL ||
        directory_backend == VFS_DIRECTORY_FAT32) return false;

    if (directory_backend == VFS_DIRECTORY_ROOT) {
        struct ramfs_entry *ram_entry = ramfs_find(name);
        if (ram_entry != NULL) {
            char normalized[NEXUS_VFS_NAME_MAX];
            if (destination_path[0] == '/' ||
                !normalize_ramfs_name(destination_path, normalized) ||
                ramfs_find(normalized) != NULL) return false;
            if (nxfs_root_mounted) {
                char root_target[NEXUS_VFS_NAME_MAX];
                if (!vfs_nxfs_compose_path(normalized, root_target) ||
                    vfs_nxfs_find_path(root_target) != NULL) return false;
            }
            for (size_t i = 0U; i < NEXUS_VFS_NAME_MAX; ++i)
                ram_entry->name[i] = normalized[i];
            return true;
        }
    }
    if (!nxfs_root_mounted || !nxfs_is_writable()) return false;
    char source[NEXUS_VFS_NAME_MAX];
    char destination[NEXUS_VFS_NAME_MAX];
    if (!vfs_nxfs_compose_path(name, source) ||
        !vfs_nxfs_resolve_target_path(destination_path, destination))
        return false;
    return nxfs_move_file(source, destination);
}

bool vfs_rename_file(const char *name, const char *new_name) {
    return vfs_move_file(name, new_name);
}

/* NXFS copies stream blocks through a bounded buffer rather than a file-sized allocation. */
bool vfs_copy_file(const char *name, const char *destination_path) {
    if (name == NULL || destination_path == NULL ||
        directory_backend == VFS_DIRECTORY_FAT32) return false;

    if (directory_backend == VFS_DIRECTORY_ROOT) {
        struct ramfs_entry *ram_entry = ramfs_find(name);
        if (ram_entry != NULL) {
            if (!nxfs_root_mounted || !nxfs_is_writable()) {
                char normalized[NEXUS_VFS_NAME_MAX];
                if (destination_path[0] == '/' ||
                    !normalize_ramfs_name(destination_path, normalized) ||
                    ramfs_find(normalized) != NULL) return false;
                char root_target[NEXUS_VFS_NAME_MAX];
                if (nxfs_root_mounted &&
                    (!vfs_nxfs_compose_path(normalized, root_target) ||
                     vfs_nxfs_find_path(root_target) != NULL)) return false;
                return vfs_ramfs_write(normalized, ram_entry->data,
                                       ram_entry->size);
            }
            if (vfs_target_is_leaf(destination_path) &&
                ramfs_find(destination_path) != NULL) return false;
            char target[NEXUS_VFS_NAME_MAX];
            if (!vfs_nxfs_resolve_target_path(destination_path, target) ||
                vfs_nxfs_find_path(target) != NULL)
                return false;
            return nxfs_write_file(target, ram_entry->data, ram_entry->size);
        }
    }

    if (!nxfs_root_mounted || !nxfs_is_writable()) return false;
    char source[NEXUS_VFS_NAME_MAX];
    char destination[NEXUS_VFS_NAME_MAX];
    if (!vfs_nxfs_compose_path(name, source) ||
        !vfs_nxfs_resolve_target_path(destination_path, destination))
        return false;
    if (directory_backend == VFS_DIRECTORY_ROOT &&
        vfs_target_is_leaf(destination_path) &&
        ramfs_find(destination_path) != NULL) return false;
    return nxfs_copy_file(source, destination);
}

bool vfs_create_directory(const char *name) {
    if (name == NULL || directory_backend == VFS_DIRECTORY_FAT32 ||
        !nxfs_root_mounted || !nxfs_is_writable()) return false;
    char path[NEXUS_VFS_NAME_MAX];
    if (!vfs_nxfs_compose_path(name, path)) return false;
    return nxfs_create_directory(path);
}

bool vfs_remove_directory(const char *name) {
    if (name == NULL || directory_backend == VFS_DIRECTORY_FAT32 ||
        !nxfs_root_mounted || !nxfs_is_writable()) return false;
    char path[NEXUS_VFS_NAME_MAX];
    if (!vfs_nxfs_compose_path(name, path)) return false;
    return nxfs_remove_directory(path);
}

static void vfs_reset_directory(void) {
    directory_depth = 0U;
    directory_backend = VFS_DIRECTORY_ROOT;
    for (size_t i = 0U; i < VFS_DIRECTORY_MAX_DEPTH; ++i) {
        directory_names[i][0] = '\0';
        directory_stack[i] = 0U;
    }
    if (root_mounted) directory_stack[0] = fat32_root_cluster();
}

bool vfs_mount_root_fat32(const struct nexus_block_device *device,
                          const struct nexus_gpt_partition *partition) {
    root_mounted = false;
    vfs_reset_directory();
    if (!fat32_mount(device, partition)) return false;
    if (!blockdev_set_mounted(device, true)) return false;
    root_mounted = true;
    directory_stack[0] = fat32_root_cluster();
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
    if (name == NULL || directory_depth + 1U >= VFS_DIRECTORY_MAX_DEPTH)
        return false;
    struct nexus_vfs_dirent selected;
    bool found = false;
    for (size_t i = 0U; i < vfs_root_entry_count(); ++i) {
        if (vfs_root_entry(i, &selected) &&
            selected.type == NEXUS_VFS_DIRECTORY &&
            name_equal(selected.name, name)) {
            found = true;
            break;
        }
    }
    if (!found) return false;

    char selected_name[NEXUS_VFS_NAME_MAX];
    size_t length = 0U;
    while (length < NEXUS_VFS_NAME_MAX - 1U &&
           selected.name[length] != '\0') {
        selected_name[length] = selected.name[length];
        ++length;
    }
    if (length == 0U) return false;
    selected_name[length] = '\0';

    if (directory_backend == VFS_DIRECTORY_FAT32 ||
        (directory_backend == VFS_DIRECTORY_ROOT &&
         selected.first_cluster >= 2U)) {
        if (!root_mounted || selected.first_cluster < 2U) return false;
        const uint32_t child_cluster = selected.first_cluster;
        for (size_t depth = 0U; depth <= directory_depth; ++depth)
            if (directory_stack[depth] == child_cluster) return false;
        if (!fat32_read_directory(child_cluster)) return false;
        directory_stack[directory_depth + 1U] = child_cluster;
        directory_backend = VFS_DIRECTORY_FAT32;
    } else {
        char path[NEXUS_VFS_NAME_MAX];
        if (!vfs_nxfs_compose_path(selected_name, path) ||
            !nxfs_is_directory(path)) return false;
        directory_backend = VFS_DIRECTORY_NXFS;
        directory_stack[directory_depth + 1U] = 0U;
    }

    for (size_t i = 0U; i <= length; ++i)
        directory_names[directory_depth][i] = selected_name[i];
    ++directory_depth;
    return true;
}

bool vfs_parent_directory(void) {
    if (directory_depth == 0U) return false;
    if (directory_backend == VFS_DIRECTORY_FAT32) {
        const uint32_t parent = directory_stack[directory_depth - 1U];
        if (!root_mounted || !fat32_read_directory(parent)) return false;
    }
    directory_names[directory_depth - 1U][0] = '\0';
    directory_stack[directory_depth] = 0U;
    --directory_depth;
    if (directory_depth == 0U)
        directory_backend = VFS_DIRECTORY_ROOT;
    return true;
}

bool vfs_mount_root_nxfs(const struct nexus_block_device *device,
                         const struct nexus_gpt_partition *partition) {
    nxfs_root_mounted = false;
    if (!nxfs_mount(device, partition)) return false;
    if (!blockdev_set_mounted(device, true)) return false;
    nxfs_root_mounted = true;
    vfs_reset_directory();
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
bool vfs_stream_write_begin(const char *name, uint32_t expected_length) {
    char path[NEXUS_VFS_NAME_MAX];
    if (vfs_stream_active || name == NULL ||
        directory_backend == VFS_DIRECTORY_FAT32 ||
        !nxfs_root_mounted || !nxfs_is_writable() ||
        !vfs_nxfs_compose_path(name, path) ||
        !nxfs_write_file_begin(path, expected_length)) return false;
    vfs_stream_active = true;
    return true;
}

bool vfs_stream_write_append(const void *data, size_t length) {
    return vfs_stream_active && nxfs_write_file_append(data, length);
}

bool vfs_stream_write_finish(void) {
    if (!vfs_stream_active) return false;
    vfs_stream_active = false;
    if (nxfs_write_file_finish()) return true;
    nxfs_write_file_abort();
    return false;
}

void vfs_stream_write_abort(void) {
    if (!vfs_stream_active) return;
    vfs_stream_active = false;
    nxfs_write_file_abort();
}

bool vfs_write_file(const char *name, const void *data, size_t length) {
    if (vfs_stream_active) return false;
    if (name == NULL || (length != 0U && data == NULL) ||
        directory_backend == VFS_DIRECTORY_FAT32) return false;
    if (nxfs_root_mounted && nxfs_is_writable()) {
        if (length > NEXUS_NXFS_MAX_FILE_BYTES) return false;
        char path[NEXUS_VFS_NAME_MAX];
        if (!vfs_nxfs_compose_path(name, path)) return false;
        return nxfs_write_file(path, data, length);
    }
    if (directory_backend != VFS_DIRECTORY_ROOT) return false;
    return vfs_ramfs_write(name, data, length);
}

/*
 * Root-scoped writes are intended for small system preferences and metadata.
 * They deliberately ignore the caller's current directory, so an application
 * cannot accidentally save configuration into whichever folder Files left
 * selected. Persistent NXFS errors are propagated; only an absent/read-only
 * NXFS volume falls back to the bounded volatile RAM overlay.
 */
bool vfs_write_root_file(const char *name, const void *data, size_t length) {
    if (!vfs_target_is_leaf(name) ||
        (length != 0U && data == NULL) ||
        length > NEXUS_NXFS_MAX_FILE_BYTES) return false;
    if (nxfs_root_mounted && nxfs_is_writable())
        return nxfs_write_file(name, data, length);
    return vfs_ramfs_write(name, data, length);
}

/*
 * Root-scoped removal mirrors root-scoped reads/writes and is restricted to
 * Nexus-owned writable storage. It never removes a same-named FAT32 source.
 */
bool vfs_remove_root_file(const char *name) {
    if (!vfs_target_is_leaf(name)) return false;
    if (ramfs_find(name) != NULL) return vfs_ramfs_remove(name);
    if (!nxfs_root_mounted || !nxfs_is_writable()) return false;
    const struct nexus_nxfs_entry *entry = vfs_nxfs_find_path(name);
    if (entry == NULL || entry->directory) return false;
    return nxfs_remove_file(name);
}

/*
 * Package paths are kept inside the NXFS Data volume. Accept only simple
 * ASCII components: no empty, dot, parent, backslash, or control segments.
 * These APIs deliberately require writable NXFS and never use RAMFS/FAT32.
 */
static bool vfs_valid_root_path(const char *path) {
    if (path == NULL) return false;
    size_t length = 0U;
    size_t component_length = 0U;
    bool previous_dot = false;
    while (length < NEXUS_VFS_NAME_MAX && path[length] != '\0') {
        if (length >= NEXUS_VFS_NAME_MAX - 1U) return false;
        const char c = path[length];
        if (c == '/') {
            if (component_length == 0U || previous_dot) return false;
            component_length = 0U;
            previous_dot = false;
        } else {
            const bool valid =
                (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
            if (!valid || (c == '.' &&
                (component_length == 0U || previous_dot))) return false;
            ++component_length;
            previous_dot = c == '.';
        }
        ++length;
    }
    if (length == 0U || length >= NEXUS_VFS_NAME_MAX ||
        path[length] != '\0' || component_length == 0U || previous_dot)
        return false;
    return true;
}
bool vfs_create_root_directory(const char *name) {
    if (!vfs_valid_root_path(name) || !nxfs_root_mounted ||
        !nxfs_is_writable()) return false;
    if (nxfs_is_directory(name)) return true;
    return nxfs_create_directory(name);
}

bool vfs_write_root_path(const char *path, const void *data, size_t length) {
    if (!vfs_valid_root_path(path) || !nxfs_root_mounted ||
        !nxfs_is_writable() || (length != 0U && data == NULL) ||
        length > NEXUS_NXFS_MAX_FILE_BYTES) return false;
    return nxfs_write_file(path, data, length);
}

bool vfs_remove_root_path(const char *path) {
    if (!vfs_valid_root_path(path) || !nxfs_root_mounted ||
        !nxfs_is_writable()) return false;
    const struct nexus_nxfs_entry *entry = vfs_nxfs_find_path(path);
    return entry != NULL && !entry->directory && nxfs_remove_file(path);
}

bool vfs_open_root_readonly_path(const char *path,
                                 struct nexus_vfs_file *file) {
    if (!vfs_valid_root_path(path) || file == NULL || !nxfs_root_mounted)
        return false;
    *file = (struct nexus_vfs_file){0};
    const struct nexus_nxfs_entry *entry = vfs_nxfs_find_path(path);
    if (entry == NULL || entry->directory) return false;
    file->filesystem_entry = entry;
    file->open = true;
    file->nxfs_file = true;
    return true;
}

bool vfs_current_directory_writable(void) {
    if (directory_backend == VFS_DIRECTORY_FAT32) return false;
    if (directory_backend == VFS_DIRECTORY_NXFS)
        return nxfs_root_mounted && nxfs_is_writable();
    return true;
}

size_t vfs_root_entry_count(void) {
    if (directory_backend == VFS_DIRECTORY_FAT32)
        return root_mounted ? fat32_root_entry_count() : 0U;
    if (directory_backend == VFS_DIRECTORY_NXFS) {
        char parent[NEXUS_VFS_NAME_MAX];
        if (!vfs_nxfs_current_path(parent)) return 0U;
        size_t count = vfs_nxfs_child_count(parent);
        /* The immutable system pack is overlaid at the user Data root only. */
        if (parent[0] == '\0') count += nexus_boot_pack_count();
        return count;
    }
    size_t count = 0U;
    for (size_t i = 0U; i < NEXUS_VFS_RAM_MAX_FILES; ++i)
        if (ramfs[i].used) ++count;
    if (nxfs_root_mounted) count += vfs_nxfs_child_count("");
    count += nexus_boot_pack_count();
    if (root_mounted) count += fat32_root_entry_count();
    return count;
}

bool vfs_root_entry(size_t index, struct nexus_vfs_dirent *entry) {
    if (entry == NULL) return false;
    if (directory_backend == VFS_DIRECTORY_FAT32) {
        const struct nexus_fat32_entry *fat = root_mounted
            ? fat32_root_entry_at(index) : NULL;
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

    if (directory_backend == VFS_DIRECTORY_NXFS) {
        char parent[NEXUS_VFS_NAME_MAX];
        char leaf[NEXUS_VFS_NAME_MAX];
        if (!vfs_nxfs_current_path(parent)) return false;
        const size_t child_count = vfs_nxfs_child_count(parent);
        if (index < child_count) {
            const struct nexus_nxfs_entry *nx_entry =
                vfs_nxfs_child_at(parent, index, leaf);
            if (nx_entry == NULL) return false;
            size_t n = 0U;
            while (n < NEXUS_VFS_NAME_MAX - 1U && leaf[n] != '\0') {
                entry->name[n] = leaf[n];
                ++n;
            }
            entry->name[n] = '\0';
            entry->type = nx_entry->directory
                ? NEXUS_VFS_DIRECTORY : NEXUS_VFS_FILE;
            entry->size = nx_entry->size;
            entry->first_cluster = 0U;
            return true;
        }
        if (parent[0] != '\0') return false;
        const struct nexus_boot_pack_entry *boot =
            nexus_boot_pack_entry_at(index - child_count);
        if (boot == NULL) return false;
        size_t n = 0U;
        while (n < NEXUS_VFS_NAME_MAX - 1U && boot->name[n] != '\0') {
            entry->name[n] = boot->name[n];
            ++n;
        }
        entry->name[n] = '\0';
        entry->type = NEXUS_VFS_FILE;
        entry->size = boot->size;
        entry->first_cluster = 0U;
        return true;
    }

    for (size_t i = 0U; i < NEXUS_VFS_RAM_MAX_FILES; ++i) {
        if (!ramfs[i].used) continue;
        if (index == 0U) {
            size_t n = 0U;
            while (ramfs[i].name[n] != '\0' &&
                   n < NEXUS_VFS_NAME_MAX - 1U) {
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
        char leaf[NEXUS_VFS_NAME_MAX];
        const struct nexus_nxfs_entry *nx_entry =
            vfs_nxfs_child_at("", index, leaf);
        if (nx_entry != NULL) {
            size_t n = 0U;
            while (n < NEXUS_VFS_NAME_MAX - 1U && leaf[n] != '\0') {
                entry->name[n] = leaf[n];
                ++n;
            }
            entry->name[n] = '\0';
            entry->type = nx_entry->directory
                ? NEXUS_VFS_DIRECTORY : NEXUS_VFS_FILE;
            entry->size = nx_entry->size;
            entry->first_cluster = 0U;
            return true;
        }
        index -= vfs_nxfs_child_count("");
    }
    const size_t boot_count = nexus_boot_pack_count();
    if (index < boot_count) {
        const struct nexus_boot_pack_entry *boot =
            nexus_boot_pack_entry_at(index);
        if (boot == NULL) return false;
        size_t n = 0U;
        while (n < NEXUS_VFS_NAME_MAX - 1U && boot->name[n] != '\0') {
            entry->name[n] = boot->name[n];
            ++n;
        }
        entry->name[n] = '\0';
        entry->type = NEXUS_VFS_FILE;
        entry->size = boot->size;
        entry->first_cluster = 0U;
        return true;
    }
    index -= boot_count;
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

    if (directory_backend == VFS_DIRECTORY_ROOT) {
        struct ramfs_entry *ram_entry = ramfs_find(name);
        if (ram_entry != NULL) {
            file->filesystem_entry = ram_entry;
            file->offset = 0U;
            file->open = true;
            file->ram_file = true;
            return true;
        }
    }
    if (directory_backend != VFS_DIRECTORY_FAT32 && nxfs_root_mounted) {
        char path[NEXUS_VFS_NAME_MAX];
        if (vfs_nxfs_compose_path(name, path)) {
            const struct nexus_nxfs_entry *entry = vfs_nxfs_find_path(path);
            if (entry != NULL && !entry->directory) {
                file->filesystem_entry = entry;
                file->offset = 0U;
                file->open = true;
                file->nxfs_file = true;
                return true;
            }
        }
    }
    if (directory_backend == VFS_DIRECTORY_ROOT ||
        (directory_backend == VFS_DIRECTORY_NXFS && directory_depth == 0U)) {
        const struct nexus_boot_pack_entry *boot = nexus_boot_pack_find(name);
        if (boot != NULL) {
            file->filesystem_entry = boot;
            file->offset = 0U;
            file->open = true;
            file->system_pack_file = true;
            return true;
        }
    }
    if (directory_backend == VFS_DIRECTORY_NXFS || !root_mounted)
        return false;
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

/*
 * Open a simple file name from the volume root without changing the current
 * directory backend or directory stack. Lookup order mirrors the root VFS
 * overlay: RAMFS, NXFS Data volume, then read-only FAT32.
 */
bool vfs_open_root_readonly(const char *name, struct nexus_vfs_file *file) {
    if (!vfs_target_is_leaf(name) || file == NULL) return false;
    *file = (struct nexus_vfs_file){0};

    struct ramfs_entry *ram_entry = ramfs_find(name);
    if (ram_entry != NULL) {
        file->filesystem_entry = ram_entry;
        file->open = true;
        file->ram_file = true;
        return true;
    }

    if (nxfs_root_mounted) {
        const struct nexus_nxfs_entry *entry = vfs_nxfs_find_path(name);
        if (entry != NULL && !entry->directory) {
            file->filesystem_entry = entry;
            file->open = true;
            file->nxfs_file = true;
            return true;
        }
    }

    const struct nexus_boot_pack_entry *boot = nexus_boot_pack_find(name);
    if (boot != NULL) {
        file->filesystem_entry = boot;
        file->open = true;
        file->system_pack_file = true;
        return true;
    }

    if (root_mounted) {
        /* FAT32 enumeration is stateful. Switch to the root to locate the
         * root entry, copy its metadata, then restore the active directory.
         */
        const uint32_t restore_cluster =
            directory_backend == VFS_DIRECTORY_FAT32 &&
            directory_depth != 0U
                ? directory_stack[directory_depth] : fat32_root_cluster();
        if (!fat32_read_directory(fat32_root_cluster())) return false;

        bool found = false;
        struct nexus_fat32_entry snapshot = {0};
        for (size_t i = 0U; i < fat32_root_entry_count(); ++i) {
            const struct nexus_fat32_entry *entry = fat32_root_entry_at(i);
            if (entry == NULL || !name_equal(entry->name, name) ||
                (entry->attributes & 0x10U) != 0U) continue;
            snapshot = *entry;
            found = true;
            break;
        }
        if (!fat32_read_directory(restore_cluster)) return false;
        if (found) {
            file->fat32_snapshot = snapshot;
            file->open = true;
            file->fat32_file = true;
            return true;
        }
    }
    return false;
}

bool vfs_file_writable(const char *name) {
    if (name == NULL || directory_backend == VFS_DIRECTORY_FAT32) return false;
    if (directory_backend == VFS_DIRECTORY_ROOT &&
        ramfs_find(name) != NULL) return true;
    if (!nxfs_root_mounted || !nxfs_is_writable()) return false;
    char path[NEXUS_VFS_NAME_MAX];
    if (!vfs_nxfs_compose_path(name, path)) return false;
    const struct nexus_nxfs_entry *entry = vfs_nxfs_find_path(path);
    return entry != NULL && !entry->directory;
}

bool vfs_file_size(const struct nexus_vfs_file *file, uint32_t *size) {
    if (file == NULL || !file->open || size == NULL) return false;
    if (file->fat32_file) {
        if (!root_mounted) return false;
        *size = file->fat32_snapshot.size;
        return true;
    }
    if (file->filesystem_entry == NULL) return false;
    if (file->system_pack_file) {
        const struct nexus_boot_pack_entry *entry =
            (const struct nexus_boot_pack_entry *)file->filesystem_entry;
        *size = entry->size;
        return true;
    }
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
        if (!nxfs_root_mounted || entry == NULL || !entry->used ||
            entry->directory) return false;
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
    if (file->system_pack_file) {
        const struct nexus_boot_pack_entry *entry =
            (const struct nexus_boot_pack_entry *)file->filesystem_entry;
        if (!nexus_boot_pack_read(entry, file->offset, buffer,
                                  requested, bytes_read)) return false;
        file->offset += (uint32_t)*bytes_read;
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
            entry->directory || file->offset > entry->size) return false;
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
