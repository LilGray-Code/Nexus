#include "package.h"
#include "process.h"

#include "elf64.h"
#include "nxfs.h"
#include "sha256.h"
#include "vfs.h"

#include <stddef.h>
#include <stdint.h>

#define PACKAGE_MAGIC "NEXUS-PACKAGE/1"
#define PACKAGE_APP_DIRECTORY "APPS"
#define PACKAGE_ENTRY_NAME "APP.ELF"
#define PACKAGE_RECEIPT_SUFFIX ".NPK"
#define PACKAGE_EXECUTABLE_SUFFIX ".ELF"

static enum nexus_package_error last_package_error;

enum nexus_package_error nexus_package_last_error(void) {
    return last_package_error;
}

static bool bytes_equal(const uint8_t *a, const char *b, size_t length) {
    if (a == NULL || b == NULL) return false;
    for (size_t i = 0U; i < length; ++i)
        if (a[i] != (uint8_t)b[i]) return false;
    return true;
}

/*
 * Compare cryptographic digests without returning at the first differing byte.
 * This avoids making the mismatch position observable through comparison time.
 */
static bool digest_equal(const uint8_t *left, const uint8_t *right,
                         size_t length) {
    if (left == NULL || right == NULL) return false;
    uint8_t difference = 0U;
    for (size_t i = 0U; i < length; ++i)
        difference |= (uint8_t)(left[i] ^ right[i]);
    return difference == 0U;
}

static bool copy_value(char *destination, size_t capacity,
                       const uint8_t *value, size_t length) {
    if (destination == NULL || value == NULL || capacity == 0U ||
        length == 0U || length >= capacity) return false;
    for (size_t i = 0U; i < length; ++i) {
        if (value[i] == 0U || value[i] == '\r' || value[i] == '\n')
            return false;
        destination[i] = (char)value[i];
    }
    destination[length] = '\0';
    return true;
}

static bool read_line(const uint8_t *bytes, size_t length, size_t *offset,
                      size_t *line_start, size_t *line_length) {
    if (bytes == NULL || offset == NULL || line_start == NULL ||
        line_length == NULL || *offset >= length) return false;
    const size_t start = *offset;
    size_t end = start;
    while (end < length && bytes[end] != '\n') {
        if (bytes[end] == '\r' || bytes[end] == 0U ||
            end - start >= NEXUS_PACKAGE_HEADER_MAX) return false;
        ++end;
    }
    if (end >= length) return false;
    *line_start = start;
    *line_length = end - start;
    *offset = end + 1U;
    return true;
}

static bool expect_value(const uint8_t *bytes, size_t start, size_t length,
                         const char *prefix, char *output,
                         size_t output_capacity) {
    size_t prefix_length = 0U;
    while (prefix[prefix_length] != '\0') ++prefix_length;
    if (length <= prefix_length ||
        !bytes_equal(bytes + start, prefix, prefix_length)) return false;
    return copy_value(output, output_capacity,
                      bytes + start + prefix_length,
                      length - prefix_length);
}

static int hex_nibble(uint8_t c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    return -1;
}

static bool decode_hex(const uint8_t *text, size_t text_length,
                       uint8_t *output, size_t output_length) {
    if (text == NULL || output == NULL ||
        text_length != output_length * 2U) return false;
    for (size_t i = 0U; i < output_length; ++i) {
        const int high = hex_nibble(text[i * 2U]);
        const int low = hex_nibble(text[i * 2U + 1U]);
        if (high < 0 || low < 0) return false;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static bool parse_decimal(const char *text, uint32_t maximum,
                          uint32_t *value) {
    if (text == NULL || value == NULL || text[0] == '\0' ||
        (text[0] == '0' && text[1] != '\0')) return false;
    uint32_t parsed = 0U;
    for (size_t i = 0U; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        const uint32_t digit = (uint32_t)(text[i] - '0');
        if (digit > maximum || parsed > (maximum - digit) / 10U)
            return false;
        parsed = parsed * 10U + digit;
    }
    if (parsed == 0U) return false;
    *value = parsed;
    return true;
}

static bool valid_id(const char *id) {
    if (id == NULL || id[0] < 'A' || id[0] > 'Z') return false;
    size_t length = 0U;
    char previous = '\0';
    while (id[length] != '\0') {
        const char c = id[length];
        const bool valid = (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '-';
        if (!valid || (c == '-' && previous == '-')) return false;
        previous = c;
        ++length;
        if (length > NEXUS_PACKAGE_ID_MAX) return false;
    }
    return length != 0U && previous != '-';
}

static bool valid_version(const char *version) {
    if (version == NULL) return false;
    size_t length = 0U;
    size_t component_start = 0U;
    uint32_t component = 0U;
    unsigned component_count = 0U;
    while (version[length] != '\0') {
        const char c = version[length];
        if (c == '.') {
            if (length == component_start) return false;
            if (component > 255U) return false;
            ++component_count;
            component_start = length + 1U;
            component = 0U;
        } else {
            if (c < '0' || c > '9') return false;
            if (length == component_start && c == '0' &&
                version[length + 1U] != '.' && version[length + 1U] != '\0')
                return false;
            component = component * 10U + (uint32_t)(c - '0');
            if (component > 255U) return false;
        }
        ++length;
        if (length > NEXUS_PACKAGE_VERSION_MAX) return false;
    }
    if (length == component_start || component_count != 2U) return false;
    return true;
}

static bool strings_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL) return false;
    size_t i = 0U;
    while (i < NEXUS_VFS_NAME_MAX) {
        if (left[i] != right[i]) return false;
        if (left[i] == '\0') return true;
        ++i;
    }
    return false;
}

static bool valid_key_id(const char *key_id) {
    if (key_id == NULL || key_id[0] == '\0') return false;
    size_t length = 0U;
    while (key_id[length] != '\0') {
        const char c = key_id[length];
        const bool valid = (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_';
        if (!valid) return false;
        ++length;
        if (length > NEXUS_PACKAGE_KEY_ID_MAX) return false;
    }
    return length != 0U;
}

static bool parse_permission_mask(const char *text,
                                   uint32_t *permissions) {
    if (text == NULL || permissions == NULL || text[0] == '\0' ||
        (text[0] == '0' && text[1] != '\0')) return false;
    uint32_t value = 0U;
    for (size_t i = 0U; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        const uint32_t digit = (uint32_t)(text[i] - '0');
        if (digit > NEXUS_APP_PERMISSION_ALL ||
            value > (NEXUS_APP_PERMISSION_ALL - digit) / 10U)
            return false;
        value = value * 10U + digit;
    }
    if ((value & ~NEXUS_APP_PERMISSION_ALL) != 0U) return false;
    *permissions = value;
    return true;
}

static bool make_permission_path(const char *id, char output[32]) {
    if (!valid_id(id) || output == NULL) return false;
    static const char prefix[] = "APPS/";
    static const char suffix[] = ".NPA";
    size_t used = 0U;
    for (size_t i = 0U; i < sizeof(prefix) - 1U; ++i)
        output[used++] = prefix[i];
    for (size_t i = 0U; id[i] != '\0'; ++i) {
        if (used >= 30U) return false;
        output[used++] = id[i];
    }
    for (size_t i = 0U; i < sizeof(suffix) - 1U; ++i) {
        if (used >= 31U) return false;
        output[used++] = suffix[i];
    }
    output[used] = '\0';
    return true;
}


static bool package_digest_hex(
    const uint8_t digest[NEXUS_PACKAGE_DIGEST_BYTES], char output[65]) {
    if (digest == NULL || output == NULL) return false;
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0U; i < NEXUS_PACKAGE_DIGEST_BYTES; ++i) {
        output[i * 2U] = digits[digest[i] >> 4];
        output[i * 2U + 1U] = digits[digest[i] & 0x0FU];
    }
    output[64] = '\0';
    return true;
}

static bool package_write_permission_record(
    const struct nexus_package_manifest *manifest, uint32_t permissions) {
    if (manifest == NULL || !valid_id(manifest->id) ||
        !valid_version(manifest->version) ||
        (permissions & ~NEXUS_APP_PERMISSION_ALL) != 0U) return false;
    char path[32];
    uint8_t bytes[192];
    char digest_hex[65];
    if (!make_permission_path(manifest->id, path) ||
        !package_digest_hex(manifest->payload_sha256, digest_hex)) return false;

    static const char prefix[] = "NEXUS-PACKAGE-GRANTS/2\nID=";
    static const char version_prefix[] = "\nVERSION=";
    static const char digest_prefix[] = "\nSHA256=";
    static const char permission_prefix[] = "\nPERMISSIONS=";
    size_t used = 0U;
#define APPEND_PERMISSION_TEXT(value) do { \
    const char *literal_ = (value); \
    for (size_t i_ = 0U; literal_[i_] != '\0'; ++i_) { \
        if (used >= sizeof(bytes)) return false; \
        bytes[used++] = (uint8_t)literal_[i_]; \
    } \
} while (0)
    APPEND_PERMISSION_TEXT(prefix);
    APPEND_PERMISSION_TEXT(manifest->id);
    APPEND_PERMISSION_TEXT(version_prefix);
    APPEND_PERMISSION_TEXT(manifest->version);
    APPEND_PERMISSION_TEXT(digest_prefix);
    APPEND_PERMISSION_TEXT(digest_hex);
    APPEND_PERMISSION_TEXT(permission_prefix);
    char reverse[10];
    size_t digits = 0U;
    uint32_t remaining = permissions;
    do {
        reverse[digits++] = (char)('0' + remaining % 10U);
        remaining /= 10U;
    } while (remaining != 0U && digits < sizeof(reverse));
    while (digits != 0U) {
        if (used >= sizeof(bytes)) return false;
        bytes[used++] = (uint8_t)reverse[--digits];
    }
    APPEND_PERMISSION_TEXT("\n");
#undef APPEND_PERMISSION_TEXT
    return vfs_write_root_path(path, bytes, used);
}

static bool package_read_permission_record(
    const struct nexus_package_manifest *manifest, uint32_t *permissions) {
    if (manifest == NULL || !valid_id(manifest->id) ||
        permissions == NULL) return false;
    *permissions = 0U; /* Missing or stale record means no user approval. */
    char path[32];
    if (!make_permission_path(manifest->id, path)) return false;
    struct nexus_vfs_file file;
    if (!vfs_open_root_readonly_path(path, &file)) return true;

    uint32_t size = 0U;
    uint8_t bytes[192];
    if (!vfs_file_size(&file, &size) || size == 0U ||
        size > sizeof(bytes)) {
        vfs_close(&file);
        return false;
    }
    size_t total = 0U;
    bool okay = true;
    while (total < size) {
        size_t amount = size - total;
        size_t actual = 0U;
        if (amount > NEXUS_VFS_READ_CHUNK) amount = NEXUS_VFS_READ_CHUNK;
        if (!vfs_read(&file, bytes + total, amount, &actual) ||
            actual != amount) {
            okay = false;
            break;
        }
        total += actual;
    }
    vfs_close(&file);
    if (!okay) return false;

    size_t offset = 0U, start = 0U, line_length = 0U;
    if (!read_line(bytes, total, &offset, &start, &line_length))
        return false;
    static const char legacy[] = "NEXUS-PACKAGE-GRANTS/1";
    if (line_length == sizeof(legacy) - 1U &&
        bytes_equal(bytes + start, legacy, line_length))
        return true; /* Discard legacy ID-only approval records. */
    static const char version_two[] = "NEXUS-PACKAGE-GRANTS/2";
    if (line_length != sizeof(version_two) - 1U ||
        !bytes_equal(bytes + start, version_two, line_length)) return false;

    char stored_id[NEXUS_PACKAGE_ID_MAX + 1U];
    char stored_version[NEXUS_PACKAGE_VERSION_MAX + 1U];
    char stored_digest[65];
    char decimal[16];
    uint32_t stored_permissions = 0U;
    if (!read_line(bytes, total, &offset, &start, &line_length) ||
        !expect_value(bytes, start, line_length, "ID=", stored_id,
                      sizeof(stored_id)) ||
        !read_line(bytes, total, &offset, &start, &line_length) ||
        !expect_value(bytes, start, line_length, "VERSION=", stored_version,
                      sizeof(stored_version)) ||
        !read_line(bytes, total, &offset, &start, &line_length) ||
        !expect_value(bytes, start, line_length, "SHA256=", stored_digest,
                      sizeof(stored_digest)) ||
        !read_line(bytes, total, &offset, &start, &line_length) ||
        !expect_value(bytes, start, line_length, "PERMISSIONS=", decimal,
                      sizeof(decimal)) ||
        offset != total ||
        !parse_permission_mask(decimal, &stored_permissions))
        return false;

    char expected_digest[65];
    if (!package_digest_hex(manifest->payload_sha256, expected_digest))
        return false;
    /* Approvals are bound to both the semantic version and exact executable. */
    if (!strings_equal(stored_id, manifest->id) ||
        !strings_equal(stored_version, manifest->version) ||
        !strings_equal(stored_digest, expected_digest))
        return true;
    *permissions = stored_permissions & manifest->permissions;
    return true;
}

bool nexus_package_is_installed(const char *id,
                                nexus_package_signature_verifier verifier,
                                void *verifier_context,
                                struct nexus_package_install_result *result) {
    if (!valid_id(id) || verifier == NULL || !vfs_nxfs_mounted()) return false;
    struct nexus_package_manifest manifest;
    char executable[32] = {0}, receipt[32] = {0};
    const char prefix[] = "APPS/";
    size_t out = 0U;
    for (size_t i = 0U; i < sizeof(prefix) - 1U; ++i)
        receipt[out++] = prefix[i];
    for (size_t i = 0U; id[i] != '\0'; ++i)
        receipt[out++] = id[i];
    for (size_t i = 0U; i < sizeof(PACKAGE_RECEIPT_SUFFIX) - 1U; ++i)
        receipt[out++] = PACKAGE_RECEIPT_SUFFIX[i];
    receipt[out] = '\0';

    uint8_t header[NEXUS_PACKAGE_HEADER_MAX];
    if (!read_receipt(receipt, header, &manifest, verifier,
                      verifier_context) ||
        !strings_equal(manifest.id, id) ||
        !make_paths(&manifest, executable, receipt) ||
        !installed_payload_matches(executable, &manifest)) return false;

    uint32_t stored_grants = 0U;
    if (!package_read_permission_record(&manifest, &stored_grants))
        return false;
    if (result != NULL) {
        for (size_t i = 0U; i < sizeof(result->id); ++i)
            result->id[i] = manifest.id[i];
        for (size_t i = 0U; i < sizeof(result->version); ++i)
            result->version[i] = manifest.version[i];
        for (size_t i = 0U; i < sizeof(result->executable_path); ++i)
            result->executable_path[i] = executable[i];
        result->requested_permissions = manifest.permissions;
        result->granted_permissions = stored_grants & manifest.permissions;
    }
    return true;
}

bool nexus_package_install(const void *package_bytes, size_t package_length,
                           nexus_package_signature_verifier verifier,
                           void *verifier_context,
                           struct nexus_package_install_result *result) {
    last_package_error = NEXUS_PACKAGE_ERROR_NONE;
    if (verifier == NULL || package_bytes == NULL) {
        last_package_error = NEXUS_PACKAGE_ERROR_ARGUMENT;
        return false;
    }
    if (!vfs_nxfs_writable()) {
        last_package_error = NEXUS_PACKAGE_ERROR_STORAGE;
        return false;
    }
    struct nexus_package_manifest manifest;
    if (!nexus_package_verify(package_bytes, package_length, verifier,
                              verifier_context, &manifest)) {
        last_package_error = NEXUS_PACKAGE_ERROR_VERIFY;
        return false;
    }

    const uint8_t *bytes = (const uint8_t *)package_bytes;
    const uint8_t *payload = bytes + manifest.payload_offset;
    struct nexus_elf64_image executable_image;
    if (!nexus_elf64_parse(payload, manifest.payload_size, &executable_image)) {
        last_package_error = NEXUS_PACKAGE_ERROR_EXECUTABLE;
        return false;
    }

    char executable[32], receipt[32];
    if (!make_paths(&manifest, executable, receipt) ||
        !vfs_create_root_directory(PACKAGE_APP_DIRECTORY)) {
        last_package_error = NEXUS_PACKAGE_ERROR_STORAGE;
        return false;
    }

    struct nexus_package_manifest old_manifest = {0};
    char old_executable[32] = {0};
    uint8_t old_header[NEXUS_PACKAGE_HEADER_MAX];
    const bool receipt_exists = read_receipt(
        receipt, old_header, &old_manifest, verifier, verifier_context);
    if (receipt_exists) {
        char ignored_receipt[32];
        if (old_manifest.id[0] == '\0' ||
            !strings_equal(old_manifest.id, manifest.id) ||
            !make_paths(&old_manifest, old_executable, ignored_receipt) ||
            !installed_payload_matches(old_executable, &old_manifest)) {
            last_package_error = NEXUS_PACKAGE_ERROR_RECEIPT;
            return false;
        }
    } else {
        struct nexus_vfs_file unknown;
        if (vfs_open_root_readonly_path(receipt, &unknown)) {
            vfs_close(&unknown);
            last_package_error = NEXUS_PACKAGE_ERROR_RECEIPT;
            return false;
        }
    }

    bool executable_exists = false;
    if (!file_matches(executable, payload, manifest.payload_size,
                      &executable_exists)) {
        last_package_error = NEXUS_PACKAGE_ERROR_VERSION_CONFLICT;
        return false;
    }
    if (!executable_exists &&
        !vfs_write_root_path(executable, payload, manifest.payload_size)) {
        last_package_error = NEXUS_PACKAGE_ERROR_WRITE_EXECUTABLE;
        return false;
    }

    /* Commit the signed receipt last: it is the installation visibility bit. */
    if (!vfs_write_root_path(receipt, bytes, manifest.header_size)) {
        if (!executable_exists) (void)vfs_remove_root_path(executable);
        last_package_error = NEXUS_PACKAGE_ERROR_COMMIT_RECEIPT;
        return false;
    }

    /* Remove the old version only after the new signed receipt is durable. */
    if (receipt_exists && old_executable[0] != '\0' &&
        !strings_equal(old_executable, executable)) {
        (void)vfs_remove_root_path(old_executable);
        char permission_path[32];
        if (make_permission_path(manifest.id, permission_path))
            (void)vfs_remove_root_path(permission_path);
    }

    if (result != NULL) {
        for (size_t i = 0U; i < sizeof(result->id); ++i)
            result->id[i] = manifest.id[i];
        for (size_t i = 0U; i < sizeof(result->version); ++i)
            result->version[i] = manifest.version[i];
        for (size_t i = 0U; i < sizeof(result->executable_path); ++i)
            result->executable_path[i] = executable[i];
        result->requested_permissions = manifest.permissions;
        uint32_t stored_grants = 0U;
        if (!package_read_permission_record(&manifest, &stored_grants))
            return false;
        result->granted_permissions = stored_grants & manifest.permissions;
    }
    return true;
}

bool nexus_package_set_permissions(
    const char *id, uint32_t granted_permissions,
    nexus_package_signature_verifier verifier, void *verifier_context) {
    if (!valid_id(id) || verifier == NULL ||
        (granted_permissions & ~NEXUS_APP_PERMISSION_ALL) != 0U)
        return false;
    if (!nexus_package_is_installed(id, verifier, verifier_context, NULL))
        return false;
    struct nexus_package_manifest manifest;
    /* Derive the receipt path only after validating the installed package. */
    char receipt_path[32];
    static const char prefix[] = "APPS/";
    size_t used = 0U;
    for (size_t i = 0U; i < sizeof(prefix) - 1U; ++i)
        receipt_path[used++] = prefix[i];
    for (size_t i = 0U; id[i] != '\0'; ++i)
        receipt_path[used++] = id[i];
    for (size_t i = 0U; i < sizeof(PACKAGE_RECEIPT_SUFFIX) - 1U; ++i)
        receipt_path[used++] = PACKAGE_RECEIPT_SUFFIX[i];
    receipt_path[used] = '\0';

    uint8_t header[NEXUS_PACKAGE_HEADER_MAX];
    if (!read_receipt(receipt_path, header, &manifest, verifier,
                      verifier_context) ||
        !strings_equal(manifest.id, id) ||
        (granted_permissions & ~manifest.permissions) != 0U)
        return false;
    return package_write_permission_record(&manifest, granted_permissions);
}

bool nexus_package_uninstall(const char *id,
                             nexus_package_signature_verifier verifier,
                             void *verifier_context) {
    struct nexus_package_install_result installed;
    if (!nexus_package_is_installed(id, verifier, verifier_context,
                                    &installed)) return false;
    char receipt[32];
    const char prefix[] = "APPS/";
    size_t out = 0U;
    for (size_t i = 0U; i < sizeof(prefix) - 1U; ++i)
        receipt[out++] = prefix[i];
    for (size_t i = 0U; id[i] != '\0'; ++i)
        receipt[out++] = id[i];
    for (size_t i = 0U; i < sizeof(PACKAGE_RECEIPT_SUFFIX) - 1U; ++i)
        receipt[out++] = PACKAGE_RECEIPT_SUFFIX[i];
    receipt[out] = '\0';

    /* Delete approval first. If cleanup fails, retain the signed receipt so
     * an orphan approval can never be revived by a later reinstall.
     */
    char permission_path[32];
    if (!make_permission_path(id, permission_path)) return false;
    struct nexus_vfs_file grant_file;
    if (vfs_open_root_readonly_path(permission_path, &grant_file)) {
        vfs_close(&grant_file);
        if (!vfs_remove_root_path(permission_path)) return false;
    }
    if (!vfs_remove_root_path(receipt)) return false;
    return vfs_remove_root_path(installed.executable_path);
}

bool nexus_package_launch(const char *id, nexus_package_signature_verifier verifier, void *verifier_context, uint64_t *exit_status) {
    if (exit_status != NULL) *exit_status = UINT64_MAX;
    struct nexus_package_install_result installed;
    if (!nexus_package_is_installed(id, verifier, verifier_context, &installed)) return false;
    struct nexus_process_result process_result = {0};
    enum nexus_process_error process_error = NEXUS_PROCESS_ERROR_NONE;
    if (!process_execute_root_file_with_permissions(
            installed.executable_path, installed.granted_permissions,
            &process_result, &process_error)) return false;
    if (exit_status != NULL) *exit_status = process_result.exit_status;
    return true;
}
