#include "package.h"
#include "process.h"
#include "security.h"
#include "elf64.h"
#include "sha256.h"
#include "vfs.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MOCK_FILES 8U
#define MOCK_FILE_CAPACITY (NEXUS_PACKAGE_MAX_PAYLOAD + NEXUS_PACKAGE_HEADER_MAX)

struct mock_file {
    bool used;
    char path[32];
    uint8_t data[MOCK_FILE_CAPACITY];
    size_t size;
};

static struct mock_file files[MOCK_FILES];
static bool apps_directory;
static bool mock_writable = true;
static char fail_write_path[32];
static char launched_path[32];
static uint64_t launched_capabilities;

static struct mock_file *mock_find(const char *path) {
    for (size_t i = 0U; i < MOCK_FILES; ++i)
        if (files[i].used && strcmp(files[i].path, path) == 0)
            return &files[i];
    return NULL;
}

bool vfs_nxfs_mounted(void) { return true; }
bool vfs_nxfs_writable(void) { return mock_writable; }

bool vfs_create_root_directory(const char *name) {
    if (!mock_writable || name == NULL || strcmp(name, "APPS") != 0)
        return false;
    apps_directory = true;
    return true;
}

bool vfs_write_root_path(const char *path, const void *data, size_t length) {
    if (!mock_writable || !apps_directory || path == NULL ||
        (length != 0U && data == NULL) ||
        strcmp(path, fail_write_path) == 0 ||
        length > MOCK_FILE_CAPACITY) return false;
    struct mock_file *file = mock_find(path);
    if (file == NULL) {
        for (size_t i = 0U; i < MOCK_FILES; ++i) {
            if (!files[i].used) {
                file = &files[i];
                break;
            }
        }
    }
    if (file == NULL || strlen(path) >= sizeof(file->path)) return false;
    file->used = true;
    strcpy(file->path, path);
    if (length != 0U) memcpy(file->data, data, length);
    file->size = length;
    return true;
}

bool vfs_remove_root_path(const char *path) {
    struct mock_file *file = path == NULL ? NULL : mock_find(path);
    if (!mock_writable || file == NULL) return false;
    memset(file, 0, sizeof(*file));
    return true;
}

bool vfs_open_root_readonly_path(const char *path,
                                 struct nexus_vfs_file *file) {
    struct mock_file *found = path == NULL ? NULL : mock_find(path);
    if (found == NULL || file == NULL) return false;
    *file = (struct nexus_vfs_file){0};
    file->filesystem_entry = found;
    file->open = true;
    file->nxfs_file = true;
    return true;
}

bool vfs_file_size(const struct nexus_vfs_file *file, uint32_t *size) {
    if (file == NULL || !file->open || file->filesystem_entry == NULL ||
        size == NULL) return false;
    const struct mock_file *found =
        (const struct mock_file *)file->filesystem_entry;
    *size = (uint32_t)found->size;
    return true;
}

bool vfs_read(struct nexus_vfs_file *file, void *buffer,
              size_t requested, size_t *bytes_read) {
    if (file == NULL || !file->open || file->filesystem_entry == NULL ||
        bytes_read == NULL || (requested != 0U && buffer == NULL)) return false;
    const struct mock_file *found =
        (const struct mock_file *)file->filesystem_entry;
    if (file->offset > found->size) return false;
    size_t amount = requested;
    if (amount > found->size - file->offset)
        amount = found->size - file->offset;
    if (amount != 0U)
        memcpy(buffer, found->data + file->offset, amount);
    file->offset += (uint32_t)amount;
    *bytes_read = amount;
    return true;
}

void vfs_close(struct nexus_vfs_file *file) {
    if (file != NULL) *file = (struct nexus_vfs_file){0};
}

bool process_execute_root_file_with_capabilities(
    const char *name, struct nexus_process_result *result,
    enum nexus_process_error *error, uint64_t capabilities) {
    if (name == NULL || result == NULL || error == NULL ||
        strlen(name) >= sizeof(launched_path)) return false;
    strcpy(launched_path, name);
    launched_capabilities = capabilities;
    result->pid = 7U;
    result->exit_status = 42U;
    *error = NEXUS_PROCESS_ERROR_NONE;
    return true;
}

/* ELF loading is separately tested; this harness isolates package plumbing. */
bool nexus_elf64_parse(const void *bytes, size_t length,
                       struct nexus_elf64_image *image) {
    if (bytes == NULL || length != 4U ||
        memcmp(bytes, "ELF", 3U) != 0 || image == NULL) return false;
    *image = (struct nexus_elf64_image){0};
    image->entry_point = 0x400000U;
    image->segment_count = 1U;
    return true;
}

static const char *find_bytes(const uint8_t *data, size_t length,
                                const char *needle) {
    const size_t needle_length = strlen(needle);
    if (data == NULL || needle_length == 0U || needle_length > length)
        return NULL;
    for (size_t start = 0U; start + needle_length <= length; ++start) {
        if (memcmp(data + start, needle, needle_length) == 0)
            return (const char *)(const void *)(data + start);
    }
    return NULL;
}

static void to_hex(const uint8_t *bytes, size_t length, char *output) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0U; i < length; ++i) {
        output[i * 2U] = digits[bytes[i] >> 4];
        output[i * 2U + 1U] = digits[bytes[i] & 15U];
    }
    output[length * 2U] = '\0';
}

/* Test-only deterministic verifier; this is not a production signature scheme. */
static bool mock_ed25519_verify(
    const char *key_id, const uint8_t *message, size_t message_length,
    const uint8_t signature[NEXUS_PACKAGE_SIGNATURE_BYTES], void *context) {
    (void)context;
    if (key_id == NULL || strcmp(key_id, "test-key") != 0 ||
        message == NULL || message_length == 0U ||
        signature == NULL) return false;
    uint8_t digest[32];
    nexus_sha256(message, message_length, digest);
    for (size_t i = 0U; i < sizeof(digest); ++i)
        if (signature[i] != digest[i] ||
            signature[i + sizeof(digest)] != digest[i]) return false;
    return true;
}

static size_t build_package(uint8_t *output, size_t capacity,
                            const char *version, const uint8_t payload[4]) {
    char digest_hex[65];
    uint8_t digest[32];
    nexus_sha256(payload, 4U, digest);
    to_hex(digest, sizeof(digest), digest_hex);

    char prefix[256];
    const int prefix_length = snprintf(prefix, sizeof(prefix),
        "NEXUS-PACKAGE/1\nID=DEMO\nVERSION=%s\nARCH=x86_64\nABI=1\n"
        "ENTRY=APP.ELF\nSIZE=4\nSHA256=%s\nKEY=test-key\n",
        version, digest_hex);
    assert(prefix_length > 0 && (size_t)prefix_length < sizeof(prefix));

    uint8_t signature[64];
    nexus_sha256(prefix, (size_t)prefix_length, signature);
    memcpy(signature + 32U, signature, 32U);
    char signature_hex[129];
    to_hex(signature, sizeof(signature), signature_hex);

    const int header_length = snprintf((char *)output, capacity,
        "%sSIGNATURE=%s\n\n", prefix, signature_hex);
    assert(header_length > prefix_length &&
           (size_t)header_length + 4U <= capacity);
    memcpy(output + (size_t)header_length, payload, 4U);
    return (size_t)header_length + 4U;
}

static void reset_mock(void) {
    memset(files, 0, sizeof(files));
    apps_directory = false;
    mock_writable = true;
    fail_write_path[0] = '\0';
}

_Static_assert(NEXUS_PACKAGE_MAX_PAYLOAD == (4U * 1024U * 1024U),
               "native Store package payload limit must be 4 MiB");

int main(void) {
    uint8_t package[512];
    const uint8_t first_payload[4] = {'E', 'L', 'F', '!'};
    size_t package_length = build_package(package, sizeof(package),
                                          "1.0.0", first_payload);
    struct nexus_package_manifest manifest;
    assert(nexus_package_parse(package, package_length, &manifest));
    assert(strcmp(manifest.id, "DEMO") == 0);
    assert(strcmp(manifest.version, "1.0.0") == 0);
    assert(manifest.abi_version == NEXUS_PACKAGE_ABI_VERSION);
    assert(manifest.payload_size == sizeof(first_payload));
    assert(nexus_package_verify(package, package_length,
                                mock_ed25519_verify, NULL, &manifest));

    uint8_t corrupted[512];
    memcpy(corrupted, package, package_length);
    corrupted[manifest.payload_offset] ^= 1U;
    assert(!nexus_package_verify(corrupted, package_length,
                                 mock_ed25519_verify, NULL, NULL));
    memcpy(corrupted, package, package_length);
    corrupted[manifest.signed_message_size + 12U] ^= 1U;
    assert(!nexus_package_verify(corrupted, package_length,
                                 mock_ed25519_verify, NULL, NULL));
    assert(!nexus_package_verify(package, package_length, NULL, NULL, NULL));

    memcpy(corrupted, package, package_length);
    const char *arch = find_bytes(corrupted, package_length, "ARCH=x86_64");
    assert(arch != NULL);
    ((char *)arch)[5] = 'a';
    assert(!nexus_package_parse(corrupted, package_length, NULL));
    assert(!nexus_package_parse(package, package_length - 1U, NULL));

    reset_mock();
    struct nexus_package_install_result installed;
    assert(nexus_package_install(package, package_length,
                                 mock_ed25519_verify, NULL, &installed));
    assert(strcmp(installed.id, "DEMO") == 0);
    assert(strcmp(installed.version, "1.0.0") == 0);
    assert(strcmp(installed.executable_path, "APPS/DEMO-1.0.0.ELF") == 0);
    assert(mock_find("APPS/DEMO-1.0.0.ELF") != NULL);
    assert(mock_find("APPS/DEMO.NPK") != NULL);
    assert(nexus_package_is_installed("DEMO", mock_ed25519_verify,
                                      NULL, &installed));
    /* Installed-image verification must reject a late-byte payload change. */
    struct mock_file *installed_image = mock_find("APPS/DEMO-1.0.0.ELF");
    assert(installed_image != NULL && installed_image->size == 4U);
    installed_image->data[3] ^= 1U;
    assert(!nexus_package_is_installed("DEMO", mock_ed25519_verify,
                                       NULL, NULL));
    installed_image->data[3] ^= 1U;
    assert(nexus_package_is_installed("DEMO", mock_ed25519_verify,
                                      NULL, &installed));
    uint64_t exit_status = UINT64_MAX;
    assert(nexus_package_launch("DEMO", mock_ed25519_verify, NULL, &exit_status));
    assert(strcmp(launched_path, "APPS/DEMO-1.0.0.ELF") == 0);
    assert(exit_status == 42U);
    assert(launched_capabilities == NEXUS_CAP_PACKAGE_DEFAULT);
    assert((launched_capabilities & (NEXUS_CAP_FILE_READ |
        NEXUS_CAP_INPUT | NEXUS_CAP_DISPLAY)) ==
        (NEXUS_CAP_FILE_READ | NEXUS_CAP_INPUT | NEXUS_CAP_DISPLAY));
    assert((launched_capabilities & (NEXUS_CAP_FILE_WRITE |
        NEXUS_CAP_AUDIO | NEXUS_CAP_GAMEPAD | NEXUS_CAP_NETWORK |
        NEXUS_CAP_ADMIN | NEXUS_CAP_DRIVER_MANAGE)) == 0U);
    assert(!nexus_package_launch("DEMO", NULL, NULL, &exit_status));

    /* An update stages the new image, commits its receipt, then reclaims old. */
    const uint8_t second_payload[4] = {'E', 'L', 'F', '?'};
    package_length = build_package(package, sizeof(package), "1.0.1",
                                   second_payload);
    if (!nexus_package_install(package, package_length,
                               mock_ed25519_verify, NULL, &installed)) {
        fprintf(stderr, "package update failed with error %d\\n",
                (int)nexus_package_last_error());
        assert(false);
    }
    assert(strcmp(installed.version, "1.0.1") == 0);
    assert(mock_find("APPS/DEMO-1.0.1.ELF") != NULL);
    assert(mock_find("APPS/DEMO-1.0.0.ELF") == NULL);
    assert(nexus_package_is_installed("DEMO", mock_ed25519_verify,
                                      NULL, &installed));
    assert(nexus_package_uninstall("DEMO", mock_ed25519_verify, NULL));
    assert(mock_find("APPS/DEMO.NPK") == NULL);
    assert(mock_find("APPS/DEMO-1.0.1.ELF") == NULL);
    assert(!nexus_package_is_installed("DEMO", mock_ed25519_verify,
                                       NULL, NULL));

    /* Failed receipt commit rolls back a newly-created executable file. */
    reset_mock();
    package_length = build_package(package, sizeof(package), "1.0.0",
                                   first_payload);
    strcpy(fail_write_path, "APPS/DEMO.NPK");
    assert(!nexus_package_install(package, package_length,
                                 mock_ed25519_verify, NULL, NULL));
    assert(mock_find("APPS/DEMO-1.0.0.ELF") == NULL);
    assert(mock_find("APPS/DEMO.NPK") == NULL);

    /* Same version with different bytes is never silently overwritten. */
    reset_mock();
    package_length = build_package(package, sizeof(package), "1.0.0",
                                   first_payload);
    assert(nexus_package_install(package, package_length,
                                 mock_ed25519_verify, NULL, NULL));
    const uint8_t conflicting_payload[4] = {'B', 'A', 'D', '!'};
    package_length = build_package(package, sizeof(package), "1.0.0",
                                   conflicting_payload);
    assert(!nexus_package_install(package, package_length,
                                  mock_ed25519_verify, NULL, NULL));

    puts("NEXUS SIGNED PACKAGE TESTS PASSED");
    return 0;
}
