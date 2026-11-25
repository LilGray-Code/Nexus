#include "package.h"

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

static bool bytes_equal(const uint8_t *a, const char *b, size_t length) {
    if (a == NULL || b == NULL) return false;
    for (size_t i = 0U; i < length; ++i)
        if (a[i] != (uint8_t)b[i]) return false;
    return true;
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
        if (parsed > (maximum - digit) / 10U) return false;
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

static bool parse_header(const void *package_bytes, size_t package_length,
                         bool require_payload,
                         struct nexus_package_manifest *manifest) {
    if (package_bytes == NULL || manifest == NULL ||
        package_length == 0U || package_length > NEXUS_NXFS_MAX_FILE_BYTES +
            NEXUS_PACKAGE_HEADER_MAX) return false;
    const uint8_t *bytes = (const uint8_t *)package_bytes;
    struct nexus_package_manifest parsed = {0};
    size_t offset = 0U, start = 0U, line_length = 0U;

    if (!read_line(bytes, package_length, &offset, &start, &line_length) ||
        line_length != sizeof(PACKAGE_MAGIC) - 1U ||
        !bytes_equal(bytes + start, PACKAGE_MAGIC, line_length)) return false;

    char temporary[32];
    if (!read_line(bytes, package_length, &offset, &start, &line_length) ||
        !expect_value(bytes, start, line_length, "ID=", parsed.id,
                      sizeof(parsed.id)) || !valid_id(parsed.id)) return false;

    if (!read_line(bytes, package_length, &offset, &start, &line_length) ||
        !expect_value(bytes, start, line_length, "VERSION=", parsed.version,
                      sizeof(parsed.version)) ||
        !valid_version(parsed.version)) return false;

    if (!read_line(bytes, package_length, &offset, &start, &line_length) ||
        line_length != sizeof("ARCH=x86_64") - 1U ||
        !bytes_equal(bytes + start, "ARCH=x86_64", line_length)) return false;

    if (!read_line(bytes, package_length, &offset, &start, &line_length) ||
        line_length != sizeof("ABI=1") - 1U ||
        !bytes_equal(bytes + start, "ABI=1", line_length)) return false;
    parsed.abi_version = NEXUS_PACKAGE_ABI_VERSION;

    if (!read_line(bytes, package_length, &offset, &start, &line_length) ||
        line_length != sizeof("ENTRY=" PACKAGE_ENTRY_NAME) - 1U ||
        !bytes_equal(bytes + start, "ENTRY=" PACKAGE_ENTRY_NAME,
                     line_length)) return false;

    if (!read_line(bytes, package_length, &offset, &start, &line_length) ||
        !expect_value(bytes, start, line_length, "SIZE=", temporary,
                      sizeof(temporary)) ||
        !parse_decimal(temporary, NEXUS_PACKAGE_MAX_PAYLOAD,
                       &parsed.payload_size)) return false;

    if (!read_line(bytes, package_length, &offset, &start, &line_length) ||
        line_length != sizeof("SHA256=") - 1U +
                       NEXUS_PACKAGE_DIGEST_BYTES * 2U ||
        !bytes_equal(bytes + start, "SHA256=", sizeof("SHA256=") - 1U) ||
        !decode_hex(bytes + start + sizeof("SHA256=") - 1U,
                    line_length - (sizeof("SHA256=") - 1U),
                    parsed.payload_sha256,
                    sizeof(parsed.payload_sha256))) return false;

    if (!read_line(bytes, package_length, &offset, &start, &line_length) ||
        !expect_value(bytes, start, line_length, "KEY=", parsed.key_id,
                      sizeof(parsed.key_id)) || !valid_key_id(parsed.key_id))
        return false;

    size_t signature_line_start = 0U;
    if (!read_line(bytes, package_length, &offset, &signature_line_start,
                   &line_length) ||
        line_length != sizeof("SIGNATURE=") - 1U +
                       NEXUS_PACKAGE_SIGNATURE_BYTES * 2U ||
        !bytes_equal(bytes + signature_line_start, "SIGNATURE=",
                     sizeof("SIGNATURE=") - 1U) ||
        !decode_hex(bytes + signature_line_start + sizeof("SIGNATURE=") - 1U,
                    line_length - (sizeof("SIGNATURE=") - 1U),
                    parsed.signature, sizeof(parsed.signature))) return false;
    parsed.signed_message_size = signature_line_start;

    if (!read_line(bytes, package_length, &offset, &start, &line_length) ||
        line_length != 0U || offset > NEXUS_PACKAGE_HEADER_MAX) return false;
    parsed.header_size = offset;
    parsed.payload_offset = offset;

    if (require_payload) {
        if (parsed.payload_size > package_length - offset ||
            package_length - offset != parsed.payload_size) return false;
    } else if (package_length != offset) {
        return false;
    }

    *manifest = parsed;
    return true;
}

bool nexus_package_parse(const void *package_bytes, size_t package_length,
                         struct nexus_package_manifest *manifest) {
    return parse_header(package_bytes, package_length, true, manifest);
}

static bool verify_signature(const uint8_t *header,
                             const struct nexus_package_manifest *manifest,
                             nexus_package_signature_verifier verifier,
                             void *context) {
    return header != NULL && manifest != NULL && verifier != NULL &&
        verifier(manifest->key_id, header, manifest->signed_message_size,
                 manifest->signature, context);
}

bool nexus_package_verify(const void *package_bytes, size_t package_length,
                          nexus_package_signature_verifier verifier,
                          void *verifier_context,
                          struct nexus_package_manifest *manifest) {
    struct nexus_package_manifest parsed;
    if (verifier == NULL ||
        !parse_header(package_bytes, package_length, true, &parsed))
        return false;
    const uint8_t *bytes = (const uint8_t *)package_bytes;
    uint8_t digest[NEXUS_PACKAGE_DIGEST_BYTES];
    nexus_sha256(bytes + parsed.payload_offset, parsed.payload_size, digest);
    for (size_t i = 0U; i < sizeof(digest); ++i) {
        if (digest[i] != parsed.payload_sha256[i]) return false;
    }
    if (!verify_signature(bytes, &parsed, verifier, verifier_context))
        return false;
    if (manifest != NULL) *manifest = parsed;
    return true;
}

static bool make_paths(const struct nexus_package_manifest *manifest,
                       char executable[32], char receipt[32]) {
    if (manifest == NULL || !valid_id(manifest->id) ||
        !valid_version(manifest->version)) return false;
    for (size_t i = 0U; i < 32U; ++i) {
        executable[i] = '\0';
        receipt[i] = '\0';
    }
    size_t out = 0U;
    const char prefix[] = "APPS/";
    for (size_t i = 0U; i < sizeof(prefix) - 1U; ++i)
        executable[out++] = prefix[i];
    for (size_t i = 0U; manifest->id[i] != '\0'; ++i)
        executable[out++] = manifest->id[i];
    executable[out++] = '-';
    for (size_t i = 0U; manifest->version[i] != '\0'; ++i)
        executable[out++] = manifest->version[i];
    const char elf_suffix[] = ".ELF";
    for (size_t i = 0U; i < sizeof(elf_suffix) - 1U; ++i)
        executable[out++] = elf_suffix[i];
    executable[out] = '\0';

    size_t receipt_length = 0U;
    for (size_t i = 0U; i < sizeof(prefix) - 1U; ++i)
        receipt[receipt_length++] = prefix[i];
    for (size_t i = 0U; manifest->id[i] != '\0'; ++i)
        receipt[receipt_length++] = manifest->id[i];
    const char suffix[] = PACKAGE_RECEIPT_SUFFIX;
    for (size_t i = 0U; i < sizeof(suffix) - 1U; ++i)
        receipt[receipt_length++] = suffix[i];
    receipt[receipt_length] = '\0';
    return out < 32U && receipt_length < 32U;
}

static bool file_matches(const char *path, const uint8_t *data,
                         size_t length, bool *exists) {
    if (exists == NULL || path == NULL || (length != 0U && data == NULL))
        return false;
    *exists = false;
    struct nexus_vfs_file file;
    if (!vfs_open_root_readonly_path(path, &file)) return true;
    *exists = true;
    uint32_t size = 0U;
    if (!vfs_file_size(&file, &size) || size != length) {
        vfs_close(&file);
        return false;
    }
    uint8_t buffer[NEXUS_VFS_READ_CHUNK];
    size_t compared = 0U;
    bool matches = true;
    while (compared < length) {
        size_t requested = length - compared;
        if (requested > sizeof(buffer)) requested = sizeof(buffer);
        size_t read = 0U;
        if (!vfs_read(&file, buffer, requested, &read) || read != requested) {
            matches = false;
            break;
        }
        for (size_t i = 0U; i < read; ++i) {
            if (buffer[i] != data[compared + i]) {
                matches = false;
                break;
            }
        }
        if (!matches) break;
        compared += read;
    }
    vfs_close(&file);
    return matches;
}

static bool read_receipt(const char *receipt_path, uint8_t header[512],
                         struct nexus_package_manifest *manifest,
                         nexus_package_signature_verifier verifier,
                         void *context) {
    struct nexus_vfs_file file;
    if (!vfs_open_root_readonly_path(receipt_path, &file)) return false;
    uint32_t size = 0U;
    if (!vfs_file_size(&file, &size) || size == 0U ||
        size > NEXUS_PACKAGE_HEADER_MAX) {
        vfs_close(&file);
        return false;
    }
    size_t total = 0U;
    while (total < size) {
        size_t amount = size - total;
        size_t read = 0U;
        if (amount > NEXUS_VFS_READ_CHUNK) amount = NEXUS_VFS_READ_CHUNK;
        if (!vfs_read(&file, header + total, amount, &read) ||
            read != amount) {
            vfs_close(&file);
            return false;
        }
        total += read;
    }
    vfs_close(&file);
    if (!parse_header(header, total, false, manifest) ||
        !verify_signature(header, manifest, verifier, context)) return false;
    return true;
}

static bool installed_payload_matches(
    const char *executable_path,
    const struct nexus_package_manifest *manifest) {
    if (executable_path == NULL || manifest == NULL) return false;
    struct nexus_vfs_file file;
    if (!vfs_open_root_readonly_path(executable_path, &file)) return false;
    uint32_t size = 0U;
    if (!vfs_file_size(&file, &size) || size != manifest->payload_size) {
        vfs_close(&file);
        return false;
    }
    struct nexus_sha256_context hash;
    nexus_sha256_init(&hash);
    uint8_t buffer[NEXUS_VFS_READ_CHUNK];
    size_t total = 0U;
    bool okay = true;
    while (total < size) {
        size_t amount = size - total;
        size_t read = 0U;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        if (!vfs_read(&file, buffer, amount, &read) || read != amount) {
            okay = false;
            break;
        }
        nexus_sha256_update(&hash, buffer, read);
        total += read;
    }
    vfs_close(&file);
    if (!okay) return false;
    uint8_t digest[NEXUS_PACKAGE_DIGEST_BYTES];
    nexus_sha256_final(&hash, digest);
    for (size_t i = 0U; i < sizeof(digest); ++i)
        if (digest[i] != manifest->payload_sha256[i]) return false;
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

    if (result != NULL) {
        for (size_t i = 0U; i < sizeof(result->id); ++i)
            result->id[i] = manifest.id[i];
        for (size_t i = 0U; i < sizeof(result->version); ++i)
            result->version[i] = manifest.version[i];
        for (size_t i = 0U; i < sizeof(result->executable_path); ++i)
            result->executable_path[i] = executable[i];
    }
    return true;
}

bool nexus_package_install(const void *package_bytes, size_t package_length,
                           nexus_package_signature_verifier verifier,
                           void *verifier_context,
                           struct nexus_package_install_result *result) {
    if (verifier == NULL || !vfs_nxfs_writable()) return false;
    struct nexus_package_manifest manifest;
    if (!nexus_package_verify(package_bytes, package_length, verifier,
                              verifier_context, &manifest)) return false;

    const uint8_t *bytes = (const uint8_t *)package_bytes;
    const uint8_t *payload = bytes + manifest.payload_offset;
    struct nexus_elf64_image executable_image;
    if (!nexus_elf64_parse(payload, manifest.payload_size, &executable_image))
        return false;

    char executable[32], receipt[32];
    if (!make_paths(&manifest, executable, receipt) ||
        !vfs_create_root_directory(PACKAGE_APP_DIRECTORY)) return false;

    struct nexus_package_manifest old_manifest = {0};
    char old_executable[32] = {0};
    uint8_t old_header[NEXUS_PACKAGE_HEADER_MAX];
    const bool receipt_exists = read_receipt(
        receipt, old_header, &old_manifest, verifier, verifier_context);
    if (receipt_exists) {
        char ignored_receipt[32];
        if (old_manifest.id[0] == '\0' ||
            !make_paths(&old_manifest, old_executable, ignored_receipt) ||
            !installed_payload_matches(old_executable, &old_manifest))
            return false;
    } else {
        struct nexus_vfs_file unknown;
        if (vfs_open_root_readonly_path(receipt, &unknown)) {
            vfs_close(&unknown);
            return false;
        }
    }

    bool executable_exists = false;
    if (!file_matches(executable, payload, manifest.payload_size,
                      &executable_exists)) return false;
    if (!executable_exists &&
        !vfs_write_root_path(executable, payload, manifest.payload_size))
        return false;

    /* Commit the signed receipt last: it is the installation visibility bit. */
    if (!vfs_write_root_path(receipt, bytes, manifest.header_size)) {
        if (!executable_exists) (void)vfs_remove_root_path(executable);
        return false;
    }

    /* Remove the old version only after the new signed receipt is durable. */
    if (receipt_exists && old_executable[0] != '\0' &&
        !strings_equal(old_executable, executable)) {
        (void)vfs_remove_root_path(old_executable);
    }

    if (result != NULL) {
        for (size_t i = 0U; i < sizeof(result->id); ++i)
            result->id[i] = manifest.id[i];
        for (size_t i = 0U; i < sizeof(result->version); ++i)
            result->version[i] = manifest.version[i];
        for (size_t i = 0U; i < sizeof(result->executable_path); ++i)
            result->executable_path[i] = executable[i];
    }
    return true;
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

    /* Remove the visibility record first; leftover payload is then an orphan. */
    if (!vfs_remove_root_path(receipt)) return false;
    return vfs_remove_root_path(installed.executable_path);
}
