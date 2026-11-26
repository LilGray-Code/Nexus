#include "game_library.h"

#include "process.h"
#include "sha256.h"
#include "timer.h"
#include "vfs.h"

#include <stddef.h>
#include <stdint.h>

#define GAME_LIBRARY_FILE "GAMES.NXL"
#define GAME_LIBRARY_MAGIC "NXGLIB01"
#define GAME_LIBRARY_VERSION 1U
#define GAME_LIBRARY_HEADER_SIZE 60U
#define GAME_LIBRARY_RECORD_SIZE 288U
#define GAME_LIBRARY_MAX_BYTES \
    (GAME_LIBRARY_HEADER_SIZE + \
     NEXUS_GAME_LIBRARY_MAX_ENTRIES * GAME_LIBRARY_RECORD_SIZE)

static struct nexus_game_library_entry entries[NEXUS_GAME_LIBRARY_MAX_ENTRIES];
static size_t entry_count;
static enum nexus_game_library_error last_error;
static uint8_t library_buffer[GAME_LIBRARY_MAX_BYTES];

static void write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_le16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static void write_le64(uint8_t *bytes, uint64_t value) {
    write_le32(bytes, (uint32_t)value);
    write_le32(bytes + 4U, (uint32_t)(value >> 32));
}

static uint64_t read_le64(const uint8_t *bytes) {
    return (uint64_t)read_le32(bytes) |
           ((uint64_t)read_le32(bytes + 4U) << 32);
}

static bool field_terminated(const char *text, size_t capacity) {
    if (text == NULL || capacity == 0U) return false;
    for (size_t i = 0U; i < capacity; ++i)
        if (text[i] == '\0') return true;
    return false;
}

static bool identifier_valid(const char *text, size_t capacity) {
    if (!field_terminated(text, capacity) || text[0] == '\0') return false;
    for (size_t i = 0U; text[i] != '\0'; ++i) {
        const char c = text[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

static bool printable_field(const char *text, size_t capacity,
                            bool allow_empty) {
    if (!field_terminated(text, capacity)) return false;
    if (!allow_empty && text[0] == '\0') return false;
    for (size_t i = 0U; text[i] != '\0'; ++i)
        if ((uint8_t)text[i] < 0x20U || (uint8_t)text[i] > 0x7EU)
            return false;
    return true;
}

static bool executable_leaf_valid(const char *path) {
    if (!printable_field(path, NEXUS_GAME_PATH_MAX + 1U, true))
        return false;
    for (size_t i = 0U; path[i] != '\0'; ++i) {
        const char c = path[i];
        if (c == '/' || c == '\\' || c == ':' || c == ' ')
            return false;
    }
    return true;
}

static bool game_entry_valid(const struct nexus_game_library_entry *entry) {
    if (entry == NULL ||
        !identifier_valid(entry->game_id, sizeof(entry->game_id)) ||
        !printable_field(entry->title, sizeof(entry->title), false) ||
        !executable_leaf_valid(entry->executable_path) ||
        !printable_field(entry->game_version,
                         sizeof(entry->game_version), true) ||
        !printable_field(entry->tested_build_id,
                         sizeof(entry->tested_build_id), true) ||
        !printable_field(entry->test_note, sizeof(entry->test_note), true))
        return false;
    if (entry->compatibility.platform < NEXUS_GAME_PLATFORM_NATIVE_NEXUS ||
        entry->compatibility.platform > NEXUS_GAME_PLATFORM_WINE ||
        entry->compatibility.graphics_api > NEXUS_GAME_GRAPHICS_DIRECT3D_12 ||
        entry->compatibility.anticheat > NEXUS_GAME_ANTICHEAT_UNKNOWN ||
        entry->compatibility.compatibility > NEXUS_GAME_COMPAT_UNSUPPORTED)
        return false;
    if (entry->compatibility.platform == NEXUS_GAME_PLATFORM_NATIVE_NEXUS &&
        entry->executable_path[0] != '\0') {
        const size_t length = __builtin_strlen(entry->executable_path);
        if (length < 5U ||
            entry->executable_path[length - 4U] != '.' ||
            entry->executable_path[length - 3U] != 'E' ||
            entry->executable_path[length - 2U] != 'L' ||
            entry->executable_path[length - 1U] != 'F') return false;
    }
    if (entry->compatibility.compatibility != NEXUS_GAME_COMPAT_UNTESTED &&
        entry->tested_build_id[0] == '\0') return false;
    return true;
}

static void copy_fixed(uint8_t *destination, size_t capacity,
                       const char *source) {
    size_t i = 0U;
    while (i + 1U < capacity && source[i] != '\0') {
        destination[i] = (uint8_t)source[i];
        ++i;
    }
    for (; i < capacity; ++i) destination[i] = 0U;
}

static bool decode_fixed(char *destination, size_t capacity,
                         const uint8_t *source) {
    if (destination == NULL || capacity == 0U || source == NULL) return false;
    size_t terminator = capacity;
    for (size_t i = 0U; i < capacity; ++i) {
        if (source[i] == 0U) {
            terminator = i;
            break;
        }
        if (source[i] < 0x20U || source[i] > 0x7EU) return false;
    }
    if (terminator == capacity) return false;
    for (size_t i = 0U; i <= terminator; ++i)
        destination[i] = (char)source[i];
    return true;
}

static void encode_entry(
    const struct nexus_game_library_entry *entry, uint8_t *bytes) {
    copy_fixed(bytes, 32U, entry->game_id);
    copy_fixed(bytes + 32U, 64U, entry->title);
    copy_fixed(bytes + 96U, 32U, entry->executable_path);
    copy_fixed(bytes + 128U, 24U, entry->game_version);
    copy_fixed(bytes + 152U, 24U, entry->tested_build_id);
    copy_fixed(bytes + 176U, 64U, entry->test_note);
    bytes[240] = (uint8_t)entry->compatibility.platform;
    bytes[241] = (uint8_t)entry->compatibility.graphics_api;
    bytes[242] = (uint8_t)entry->compatibility.anticheat;
    bytes[243] = (uint8_t)entry->compatibility.compatibility;
    write_le64(bytes + 244U,
               entry->compatibility.additional_required_capabilities);
    write_le64(bytes + 252U, entry->compatibility.tested_capabilities);
    write_le16(bytes + 260U,
               entry->compatibility.tested_gpu_vendor_id);
    write_le16(bytes + 262U,
               entry->compatibility.tested_gpu_device_id);
    write_le32(bytes + 264U,
               entry->compatibility.tested_gpu_driver_version);
    write_le32(bytes + 268U, entry->launch_count);
    write_le64(bytes + 272U, entry->last_played_ticks);
    write_le64(bytes + 280U, entry->tested_at_ticks);
}

static bool decode_entry(const uint8_t *bytes,
                         struct nexus_game_library_entry *entry) {
    *entry = (struct nexus_game_library_entry){0};
    if (!decode_fixed(entry->game_id, sizeof(entry->game_id), bytes) ||
        !decode_fixed(entry->title, sizeof(entry->title), bytes + 32U) ||
        !decode_fixed(entry->executable_path,
                      sizeof(entry->executable_path), bytes + 96U) ||
        !decode_fixed(entry->game_version,
                      sizeof(entry->game_version), bytes + 128U) ||
        !decode_fixed(entry->tested_build_id,
                      sizeof(entry->tested_build_id), bytes + 152U) ||
        !decode_fixed(entry->test_note,
                      sizeof(entry->test_note), bytes + 176U))
        return false;
    entry->compatibility.platform =
        (enum nexus_game_platform)bytes[240];
    entry->compatibility.graphics_api =
        (enum nexus_game_graphics_api)bytes[241];
    entry->compatibility.anticheat =
        (enum nexus_game_anticheat)bytes[242];
    entry->compatibility.compatibility =
        (enum nexus_game_compatibility)bytes[243];
    entry->compatibility.additional_required_capabilities =
        read_le64(bytes + 244U);
    entry->compatibility.tested_capabilities = read_le64(bytes + 252U);
    entry->compatibility.tested_gpu_vendor_id = read_le16(bytes + 260U);
    entry->compatibility.tested_gpu_device_id = read_le16(bytes + 262U);
    entry->compatibility.tested_gpu_driver_version = read_le32(bytes + 264U);
    entry->launch_count = read_le32(bytes + 268U);
    entry->last_played_ticks = read_le64(bytes + 272U);
    entry->tested_at_ticks = read_le64(bytes + 280U);
    return game_entry_valid(entry);
}

static bool equal_text(const char *left, const char *right) {
    if (left == NULL || right == NULL) return false;
    size_t i = 0U;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return left[i] == right[i];
}

static size_t find_entry(const char *game_id) {
    if (game_id == NULL) return NEXUS_GAME_LIBRARY_MAX_ENTRIES;
    for (size_t i = 0U; i < entry_count; ++i)
        if (equal_text(entries[i].game_id, game_id)) return i;
    return NEXUS_GAME_LIBRARY_MAX_ENTRIES;
}

void nexus_game_library_init(void) {
    for (size_t i = 0U; i < NEXUS_GAME_LIBRARY_MAX_ENTRIES; ++i)
        entries[i] = (struct nexus_game_library_entry){0};
    entry_count = 0U;
    last_error = NEXUS_GAME_LIBRARY_ERROR_NONE;
}

size_t nexus_game_library_count(void) {
    return entry_count;
}

const struct nexus_game_library_entry *nexus_game_library_at(size_t index) {
    return index < entry_count ? &entries[index] : NULL;
}

bool nexus_game_library_upsert(const struct nexus_game_library_entry *entry) {
    if (!game_entry_valid(entry)) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_ARGUMENT;
        return false;
    }
    const size_t existing = find_entry(entry->game_id);
    if (existing < NEXUS_GAME_LIBRARY_MAX_ENTRIES) {
        entries[existing] = *entry;
        last_error = NEXUS_GAME_LIBRARY_ERROR_NONE;
        return true;
    }
    if (entry_count >= NEXUS_GAME_LIBRARY_MAX_ENTRIES) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_FULL;
        return false;
    }
    entries[entry_count++] = *entry;
    last_error = NEXUS_GAME_LIBRARY_ERROR_NONE;
    return true;
}

bool nexus_game_library_remove(const char *game_id) {
    if (!identifier_valid(game_id, NEXUS_GAME_ID_MAX + 1U)) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_ARGUMENT;
        return false;
    }
    const size_t index = find_entry(game_id);
    if (index >= entry_count) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_NOT_FOUND;
        return false;
    }
    for (size_t i = index + 1U; i < entry_count; ++i)
        entries[i - 1U] = entries[i];
    --entry_count;
    entries[entry_count] = (struct nexus_game_library_entry){0};
    last_error = NEXUS_GAME_LIBRARY_ERROR_NONE;
    return true;
}

bool nexus_game_library_save(void) {
    if (!vfs_nxfs_writable()) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_STORAGE;
        return false;
    }
    const size_t payload_size = entry_count * GAME_LIBRARY_RECORD_SIZE;
    const size_t total_size = GAME_LIBRARY_HEADER_SIZE + payload_size;
    for (size_t i = 0U; i < total_size; ++i) library_buffer[i] = 0U;

    for (size_t i = 0U; i < sizeof(GAME_LIBRARY_MAGIC) - 1U; ++i)
        library_buffer[i] = (uint8_t)GAME_LIBRARY_MAGIC[i];
    write_le32(library_buffer + 8U, GAME_LIBRARY_VERSION);
    write_le32(library_buffer + 12U, GAME_LIBRARY_HEADER_SIZE);
    write_le32(library_buffer + 16U, (uint32_t)entry_count);
    write_le32(library_buffer + 20U, GAME_LIBRARY_RECORD_SIZE);
    write_le32(library_buffer + 24U, (uint32_t)payload_size);
    for (size_t i = 0U; i < entry_count; ++i)
        encode_entry(&entries[i],
            library_buffer + GAME_LIBRARY_HEADER_SIZE +
            i * GAME_LIBRARY_RECORD_SIZE);
    nexus_sha256(library_buffer + GAME_LIBRARY_HEADER_SIZE,
                 payload_size, library_buffer + 28U);
    if (!vfs_write_root_path(GAME_LIBRARY_FILE, library_buffer, total_size)) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_STORAGE;
        return false;
    }
    last_error = NEXUS_GAME_LIBRARY_ERROR_NONE;
    return true;
}

bool nexus_game_library_load(void) {
    struct nexus_vfs_file file;
    if (!vfs_open_root_readonly_path(GAME_LIBRARY_FILE, &file)) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_NOT_FOUND;
        return false;
    }
    uint32_t size = 0U;
    size_t received = 0U;
    const bool read_ok =
        vfs_file_size(&file, &size) && size >= GAME_LIBRARY_HEADER_SIZE &&
        size <= sizeof(library_buffer) &&
        vfs_read(&file, library_buffer, size, &received) &&
        received == size;
    vfs_close(&file);
    if (!read_ok) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_FORMAT;
        return false;
    }

    static const uint8_t magic[8] = {
        'N', 'X', 'G', 'L', 'I', 'B', '0', '1'
    };
    for (size_t i = 0U; i < sizeof(magic); ++i) {
        if (library_buffer[i] != magic[i]) {
            last_error = NEXUS_GAME_LIBRARY_ERROR_FORMAT;
            return false;
        }
    }
    const uint32_t count = read_le32(library_buffer + 16U);
    const uint32_t payload_size = read_le32(library_buffer + 24U);
    if (read_le32(library_buffer + 8U) != GAME_LIBRARY_VERSION ||
        read_le32(library_buffer + 12U) != GAME_LIBRARY_HEADER_SIZE ||
        read_le32(library_buffer + 20U) != GAME_LIBRARY_RECORD_SIZE ||
        count > NEXUS_GAME_LIBRARY_MAX_ENTRIES ||
        payload_size != count * GAME_LIBRARY_RECORD_SIZE ||
        size != GAME_LIBRARY_HEADER_SIZE + payload_size) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_FORMAT;
        return false;
    }
    uint8_t digest[NEXUS_SHA256_SIZE];
    nexus_sha256(library_buffer + GAME_LIBRARY_HEADER_SIZE,
                 payload_size, digest);
    uint8_t difference = 0U;
    for (size_t i = 0U; i < sizeof(digest); ++i)
        difference |= (uint8_t)(digest[i] ^ library_buffer[28U + i]);
    if (difference != 0U) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_CHECKSUM;
        return false;
    }

    struct nexus_game_library_entry decoded[NEXUS_GAME_LIBRARY_MAX_ENTRIES];
    for (size_t i = 0U; i < count; ++i) {
        if (!decode_entry(
                library_buffer + GAME_LIBRARY_HEADER_SIZE +
                    i * GAME_LIBRARY_RECORD_SIZE,
                &decoded[i])) {
            last_error = NEXUS_GAME_LIBRARY_ERROR_FORMAT;
            return false;
        }
        for (size_t j = 0U; j < i; ++j) {
            if (equal_text(decoded[i].game_id, decoded[j].game_id)) {
                last_error = NEXUS_GAME_LIBRARY_ERROR_FORMAT;
                return false;
            }
        }
    }
    for (size_t i = 0U; i < count; ++i) entries[i] = decoded[i];
    for (size_t i = count; i < NEXUS_GAME_LIBRARY_MAX_ENTRIES; ++i)
        entries[i] = (struct nexus_game_library_entry){0};
    entry_count = count;
    last_error = NEXUS_GAME_LIBRARY_ERROR_NONE;
    return true;
}

bool nexus_game_library_record_test(
    const char *game_id, enum nexus_game_compatibility compatibility,
    uint64_t tested_capabilities, uint16_t gpu_vendor_id,
    uint16_t gpu_device_id, uint32_t gpu_driver_version,
    const char *tested_build_id, const char *test_note) {
    if (!identifier_valid(game_id, NEXUS_GAME_ID_MAX + 1U) ||
        compatibility <= NEXUS_GAME_COMPAT_UNTESTED ||
        compatibility > NEXUS_GAME_COMPAT_UNSUPPORTED ||
        !printable_field(tested_build_id, NEXUS_GAME_VERSION_MAX + 1U,
                         false) ||
        !printable_field(test_note, NEXUS_GAME_TEST_NOTE_MAX + 1U, true)) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_ARGUMENT;
        return false;
    }
    const size_t index = find_entry(game_id);
    if (index >= entry_count) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_NOT_FOUND;
        return false;
    }
    entries[index].compatibility.compatibility = compatibility;
    entries[index].compatibility.tested_capabilities = tested_capabilities;
    entries[index].compatibility.tested_gpu_vendor_id = gpu_vendor_id;
    entries[index].compatibility.tested_gpu_device_id = gpu_device_id;
    entries[index].compatibility.tested_gpu_driver_version = gpu_driver_version;
    copy_fixed((uint8_t *)entries[index].tested_build_id,
               sizeof(entries[index].tested_build_id), tested_build_id);
    copy_fixed((uint8_t *)entries[index].test_note,
               sizeof(entries[index].test_note), test_note);
    entries[index].tested_at_ticks = timer_ticks();
    last_error = NEXUS_GAME_LIBRARY_ERROR_NONE;
    return true;
}

enum nexus_game_library_error nexus_game_library_last_error(void) {
    return last_error;
}

bool nexus_game_library_launch(
    const char *game_id,
    const struct nexus_gaming_system_capabilities *system,
    bool allow_untested,
    uint64_t *exit_status) {
    if (!identifier_valid(game_id, NEXUS_GAME_ID_MAX + 1U) ||
        system == NULL || exit_status == NULL) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_ARGUMENT;
        return false;
    }
    const size_t index = find_entry(game_id);
    if (index >= entry_count) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_NOT_FOUND;
        return false;
    }

    struct nexus_game_library_entry *entry = &entries[index];
    struct nexus_game_assessment assessment;
    if (!nexus_gaming_assess(&entry->compatibility, system, &assessment)) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_ARGUMENT;
        return false;
    }
    if (assessment.status == NEXUS_GAME_ASSESS_BLOCKED_ANTICHEAT) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_ANTICHEAT;
        return false;
    }
    if (assessment.status == NEXUS_GAME_ASSESS_TESTED_UNSUPPORTED) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_EXECUTION;
        return false;
    }
    if (assessment.status == NEXUS_GAME_ASSESS_MISSING_PREREQUISITES) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_PREREQUISITES;
        return false;
    }
    /* An explicit untested opt-in does not waive a hardware/driver retest. */
    if (assessment.status == NEXUS_GAME_ASSESS_NEEDS_RETEST) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_RETEST;
        return false;
    }
    if (entry->compatibility.platform != NEXUS_GAME_PLATFORM_NATIVE_NEXUS ||
        entry->executable_path[0] == '\0') {
        last_error = NEXUS_GAME_LIBRARY_ERROR_BACKEND;
        return false;
    }
    if (!allow_untested &&
        assessment.status != NEXUS_GAME_ASSESS_VERIFIED &&
        assessment.status != NEXUS_GAME_ASSESS_PLAYABLE_LIMITATIONS) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_UNTESTED;
        return false;
    }

    struct nexus_process_result result;
    enum nexus_process_error process_error = NEXUS_PROCESS_ERROR_NONE;
    if (!process_execute_root_file(
            entry->executable_path, &result, &process_error)) {
        last_error = NEXUS_GAME_LIBRARY_ERROR_EXECUTION;
        return false;
    }
    if (entry->launch_count != UINT32_MAX) ++entry->launch_count;
    entry->last_played_ticks = timer_ticks();
    *exit_status = result.exit_status;
    /* Execution success must not be revoked by a read-only/missing data disk. */
    (void)nexus_game_library_save();
    last_error = NEXUS_GAME_LIBRARY_ERROR_NONE;
    return true;
}
