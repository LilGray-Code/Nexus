#include "app_manager.h"
#include "memory.h"
#include "package.h"
#include "sha256.h"
#include "vfs.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Host-side doubles for the kernel storage, downloader and package services. */
static uint8_t stage_bytes[64];
static uint32_t stage_length;
static char stage_name[32];
static bool stage_present;
static uint8_t allocated[4096];
static bool memory_allocated;
static char installed_ids[8][NEXUS_PACKAGE_ID_MAX + 1U];
static char installed_versions[8][NEXUS_PACKAGE_VERSION_MAX + 1U];
static size_t installed_count;
static char launch_id[NEXUS_PACKAGE_ID_MAX + 1U];
static unsigned download_calls;
static unsigned install_calls;
static unsigned uninstall_calls;
static bool fail_editor_install;
static bool pause_first_download;
static bool fail_first_download;
static enum nexus_download_state download_state;
static enum nexus_download_error download_error;

bool memory_alloc_contiguous_pages(uint64_t pages,
                                   uint64_t max_address_exclusive,
                                   uint64_t *physical) {
    (void)max_address_exclusive;
    if (pages != 1U || physical == NULL || memory_allocated) return false;
    memory_allocated = true;
    *physical = 0x1000U;
    return true;
}
void *memory_phys_to_virt(uint64_t physical) {
    return physical == 0x1000U && memory_allocated ? allocated : NULL;
}
bool memory_free_contiguous_pages(uint64_t physical, uint64_t pages) {
    if (physical != 0x1000U || pages != 1U || !memory_allocated) return false;
    memory_allocated = false;
    return true;
}

bool vfs_open_root_readonly_path(const char *path,
                                 struct nexus_vfs_file *file) {
    if (path == NULL || file == NULL) return false;
    if (stage_present && strcmp(path, stage_name) == 0) {
        *file = (struct nexus_vfs_file){.open = true};
        return true;
    }
    for (size_t i = 0U; i < installed_count; ++i) {
        char receipt[32];
        (void)snprintf(receipt, sizeof(receipt), "APPS/%s.NPK",
                       installed_ids[i]);
        if (strcmp(path, receipt) == 0) {
            *file = (struct nexus_vfs_file){.open = true};
            return true;
        }
    }
    return false;
}
bool vfs_file_size(const struct nexus_vfs_file *file, uint32_t *size) {
    if (file == NULL || size == NULL || !file->open || !stage_present)
        return false;
    *size = stage_length;
    return true;
}
bool vfs_read(struct nexus_vfs_file *file, void *buffer,
              size_t requested, size_t *bytes_read) {
    if (file == NULL || buffer == NULL || bytes_read == NULL ||
        !file->open || file->offset > stage_length ||
        requested > stage_length - file->offset) return false;
    memcpy(buffer, stage_bytes + file->offset, requested);
    file->offset += (uint32_t)requested;
    *bytes_read = requested;
    return true;
}
void vfs_close(struct nexus_vfs_file *file) { if (file != NULL) file->open=false; }
bool vfs_remove_root_path(const char *path) {
    if (path == NULL || !stage_present || strcmp(path, stage_name) != 0)
        return false;
    stage_present = false;
    stage_length = 0U;
    return true;
}

bool nexus_download_https_context_init(
    struct nexus_download_https_context *context, uint32_t expected_size) {
    if (context == NULL || expected_size == 0U) return false;
    context->expected_size = expected_size;
    return true;
}
const struct nexus_download_source_ops *nexus_download_https_source_ops(void) {
    static const struct nexus_download_source_ops ops = {0};
    return &ops;
}
bool nexus_download_start(
    const char *url, const char *destination, uint32_t total,
    const uint8_t digest[32], const struct nexus_download_source_ops *source,
    void *source_context, nexus_download_progress_fn progress,
    void *progress_context) {
    (void)digest; (void)source; (void)source_context;
    (void)progress; (void)progress_context;
    assert(url != NULL && destination != NULL && total == 10U);
    ++download_calls;
    strcpy(stage_name, destination);
    if (strstr(url, "/core.nup") != NULL)
        memcpy(stage_bytes, "CORE-NPK!!", 10U);
    else
        memcpy(stage_bytes, "EDITOR-NPK", 10U);
    stage_length = 10U;
    stage_present = !fail_first_download;
    if (fail_first_download) {
        fail_first_download = false;
        download_state = NEXUS_DOWNLOAD_FAILED;
        download_error = NEXUS_DOWNLOAD_ERROR_NETWORK;
        return false;
    }
    if (pause_first_download) {
        pause_first_download = false;
        download_state = NEXUS_DOWNLOAD_PAUSED;
    } else {
        download_state = NEXUS_DOWNLOAD_COMPLETE;
    }
    download_error = NEXUS_DOWNLOAD_ERROR_NONE;
    return download_state == NEXUS_DOWNLOAD_COMPLETE ||
           download_state == NEXUS_DOWNLOAD_PAUSED;
}
bool nexus_download_resume(nexus_download_progress_fn progress,
                           void *context) {
    (void)progress; (void)context;
    if (download_state != NEXUS_DOWNLOAD_PAUSED) return false;
    download_state = NEXUS_DOWNLOAD_COMPLETE;
    return true;
}
bool nexus_download_retry(nexus_download_progress_fn progress, void *context) {
    (void)progress; (void)context;
    if (strstr(stage_name, "DL-CORE") != NULL)
        memcpy(stage_bytes, "CORE-NPK!!", 10U);
    else
        memcpy(stage_bytes, "EDITOR-NPK", 10U);
    stage_length = 10U;
    stage_present = true;
    download_error = NEXUS_DOWNLOAD_ERROR_NONE;
    download_state = NEXUS_DOWNLOAD_COMPLETE;
    return true;
}
bool nexus_download_cancel(void) {
    if (download_state != NEXUS_DOWNLOAD_PAUSED &&
        download_state != NEXUS_DOWNLOAD_ACTIVE) return false;
    download_state = NEXUS_DOWNLOAD_CANCELLED;
    return true;
}
bool nexus_download_get_status(struct nexus_download_status *status) {
    if (status == NULL) return false;
    *status = (struct nexus_download_status){
        .state = download_state,
        .error = download_error,
        .bytes_received = download_state == NEXUS_DOWNLOAD_COMPLETE ? 10U : 4U,
        .total_bytes = 10U,
        .percent = download_state == NEXUS_DOWNLOAD_COMPLETE ? 100U : 40U
    };
    strcpy(status->destination, stage_name);
    return true;
}

bool nexus_package_is_installed(
    const char *id, nexus_package_signature_verifier verifier,
    void *context, struct nexus_package_install_result *result) {
    (void)verifier; (void)context;
    for (size_t i = 0U; i < installed_count; ++i) {
        if (strcmp(installed_ids[i], id) == 0) {
            if (result != NULL) {
                strcpy(result->id, installed_ids[i]);
                strcpy(result->version, installed_versions[i]);
            }
            return true;
        }
    }
    return false;
}
bool nexus_package_verify(
    const void *bytes, size_t length,
    nexus_package_signature_verifier verifier, void *context,
    struct nexus_package_manifest *manifest) {
    (void)verifier; (void)context;
    if (bytes == NULL || length != 10U || manifest == NULL) return false;
    memset(manifest, 0, sizeof(*manifest));
    if (memcmp(bytes, "CORE-NPK!!", 10U) == 0) {
        strcpy(manifest->id, "CORE");
        strcpy(manifest->version, "1.0.0");
    } else if (memcmp(bytes, "EDITOR-NPK", 10U) == 0) {
        strcpy(manifest->id, "EDITOR");
        strcpy(manifest->version, "2.1.0");
    } else {
        return false;
    }
    manifest->payload_size = 128U;
    return true;
}
bool nexus_package_install(
    const void *bytes, size_t length,
    nexus_package_signature_verifier verifier, void *context,
    struct nexus_package_install_result *result) {
    (void)verifier; (void)context;
    assert(bytes != NULL && length == 10U);
    ++install_calls;
    struct nexus_package_manifest manifest;
    assert(nexus_package_verify(bytes, length, NULL, NULL, &manifest));
    if (fail_editor_install && strcmp(manifest.id, "EDITOR") == 0)
        return false;
    size_t slot = installed_count;
    for (size_t i = 0U; i < installed_count; ++i)
        if (strcmp(installed_ids[i], manifest.id) == 0) slot = i;
    if (slot == installed_count) ++installed_count;
    strcpy(installed_ids[slot], manifest.id);
    strcpy(installed_versions[slot], manifest.version);
    if (result != NULL) {
        strcpy(result->id, manifest.id);
        strcpy(result->version, manifest.version);
        strcpy(result->executable_path, strcmp(manifest.id, "CORE") == 0
            ? "APPS/CORE-1.0.0.ELF" : "APPS/EDITOR-2.1.0.ELF");
    }
    return true;
}
bool nexus_package_uninstall(const char *id,
                             nexus_package_signature_verifier verifier,
                             void *context) {
    (void)verifier; (void)context;
    ++uninstall_calls;
    for (size_t i = 0U; i < installed_count; ++i) {
        if (strcmp(installed_ids[i], id) == 0) {
            for (size_t j = i + 1U; j < installed_count; ++j) {
                strcpy(installed_ids[j - 1U], installed_ids[j]);
                strcpy(installed_versions[j - 1U], installed_versions[j]);
            }
            --installed_count;
            return true;
        }
    }
    return false;
}
bool nexus_package_launch(const char *id,
                          nexus_package_signature_verifier verifier,
                          void *context, uint64_t *exit_status) {
    (void)verifier; (void)context;
    strcpy(launch_id, id);
    if (exit_status != NULL) *exit_status = 7U;
    return true;
}

static void hex_encode(const uint8_t *bytes, size_t length, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0U; i < length; ++i) {
        out[2U * i] = digits[bytes[i] >> 4];
        out[2U * i + 1U] = digits[bytes[i] & 15U];
    }
    out[2U * length] = '\0';
}
static bool test_verifier(const char *key, const uint8_t *message,
                          size_t length, const uint8_t signature[64],
                          void *context) {
    (void)context;
    if (key == NULL || strcmp(key, "repo-test") != 0 ||
        message == NULL || length == 0U || signature == NULL) return false;
    uint8_t digest[32];
    nexus_sha256(message, length, digest);
    return memcmp(digest, signature, 32U) == 0 &&
           memcmp(digest, signature + 32U, 32U) == 0;
}

static size_t make_repository(char *output, size_t capacity) {
    static const uint8_t core_package[10] = "CORE-NPK!!";
    static const uint8_t editor_package[10] = "EDITOR-NPK";
    uint8_t core_digest[32], editor_digest[32];
    char core_hex[65], editor_hex[65];
    nexus_sha256(core_package, sizeof(core_package), core_digest);
    nexus_sha256(editor_package, sizeof(editor_package), editor_digest);
    hex_encode(core_digest, sizeof(core_digest), core_hex);
    hex_encode(editor_digest, sizeof(editor_digest), editor_hex);
    char lines[1024];
    const int count = snprintf(lines, sizeof(lines),
        "APP=CORE|1.0.0|10|https://repo.example/core.nup|%s|Core|Nexus|SYSTEM|Core runtime||0|0|-\n"
        "APP=EDITOR|2.1.0|10|https://repo.example/editor.nup|%s|Editor|Nexus|DEVELOPMENT|Code editor||0|0|CORE@1.0.0\n",
        core_hex, editor_hex);
    assert(count > 0 && (size_t)count < sizeof(lines));
    char prefix[1400];
    const int prefix_length = snprintf(prefix, sizeof(prefix),
        "NEXUS-APP-REPOSITORY/1\nGENERATION=1\nCOUNT=2\nKEY=repo-test\n%s",
        lines);
    assert(prefix_length > 0 && (size_t)prefix_length < sizeof(prefix));
    uint8_t signature[64];
    nexus_sha256(prefix, (size_t)prefix_length, signature);
    memcpy(signature + 32U, signature, 32U);
    char signature_hex[129];
    hex_encode(signature, sizeof(signature), signature_hex);
    const int total = snprintf(output, capacity, "%sSIGNATURE=%s\n",
                               prefix, signature_hex);
    assert(total > prefix_length && (size_t)total < capacity);
    return (size_t)total;
}

static void reset_fixture(void) {
    stage_length = 0U;
    stage_name[0] = '\0';
    stage_present = false;
    memory_allocated = false;
    installed_count = 0U;
    launch_id[0] = '\0';
    download_calls = install_calls = uninstall_calls = 0U;
    fail_editor_install = false;
    pause_first_download = false;
    fail_first_download = false;
    download_state = NEXUS_DOWNLOAD_IDLE;
    download_error = NEXUS_DOWNLOAD_ERROR_NONE;
    memset(installed_ids, 0, sizeof(installed_ids));
    memset(installed_versions, 0, sizeof(installed_versions));
}

static void make_manager(struct nexus_app_manager *manager) {
    static char repository[2048];
    const size_t length = make_repository(repository, sizeof(repository));
    assert(nexus_app_manager_init(manager, repository, length,
        test_verifier, test_verifier, NULL, NULL, NULL));
}

static void test_dependency_install_launch_and_remove(void) {
    reset_fixture();
    struct nexus_app_manager manager;
    make_manager(&manager);
    uint8_t matches[4];
    assert(nexus_app_manager_search(&manager, "edit", NULL,
                                    matches, 4U) == 1U);
    assert(matches[0] == 1U);
    assert(nexus_app_manager_install(&manager, "EDITOR"));
    assert(download_calls == 2U && install_calls == 2U);
    assert(installed_count == 2U);
    assert(strcmp(installed_ids[0], "CORE") == 0);
    assert(strcmp(installed_ids[1], "EDITOR") == 0);
    assert(!stage_present && !memory_allocated);

    uint64_t exit_status = 0U;
    assert(nexus_app_manager_launch(&manager, "EDITOR", &exit_status));
    assert(strcmp(launch_id, "EDITOR") == 0 && exit_status == 7U);
    assert(!nexus_app_manager_remove(&manager, "CORE"));
    assert(nexus_app_manager_remove(&manager, "EDITOR"));
    assert(nexus_app_manager_remove(&manager, "CORE"));
    assert(installed_count == 0U);
}

static void test_pause_resume_and_retry(void) {
    reset_fixture();
    pause_first_download = true;
    struct nexus_app_manager manager;
    make_manager(&manager);
    assert(nexus_app_manager_install(&manager, "EDITOR"));
    struct nexus_app_manager_status status;
    assert(nexus_app_manager_get_status(&manager, &status));
    assert(status.state == NEXUS_APP_MANAGER_PAUSED);
    assert(status.current_app_id[0] != '\0');
    assert(installed_count == 0U);
    assert(nexus_app_manager_resume(&manager));
    assert(download_calls == 2U && installed_count == 2U);
    assert(nexus_app_manager_get_status(&manager, &status));
    assert(status.state == NEXUS_APP_MANAGER_COMPLETE);
    assert(status.total_steps == 2U && status.completed_steps == 2U);
}

static void test_existing_application_updates_in_place(void) {
    reset_fixture();
    strcpy(installed_ids[0], "CORE");
    strcpy(installed_versions[0], "1.0.0");
    strcpy(installed_ids[1], "EDITOR");
    strcpy(installed_versions[1], "2.0.0");
    installed_count = 2U;
    struct nexus_app_manager manager;
    make_manager(&manager);
    assert(nexus_app_manager_install(&manager, "EDITOR"));
    assert(download_calls == 1U && install_calls == 1U);
    assert(installed_count == 2U);
    assert(strcmp(installed_ids[0], "CORE") == 0);
    assert(strcmp(installed_versions[0], "1.0.0") == 0);
    assert(strcmp(installed_ids[1], "EDITOR") == 0);
    assert(strcmp(installed_versions[1], "2.1.0") == 0);
    assert(uninstall_calls == 0U);
}

static void test_network_failure_can_retry_from_zero(void) {
    reset_fixture();
    fail_first_download = true;
    struct nexus_app_manager manager;
    make_manager(&manager);
    assert(!nexus_app_manager_install(&manager, "EDITOR"));
    struct nexus_app_manager_status status;
    assert(nexus_app_manager_get_status(&manager, &status));
    assert(status.state == NEXUS_APP_MANAGER_FAILED);
    assert(status.error == NEXUS_APP_MANAGER_ERROR_DOWNLOAD);
    assert(nexus_app_manager_retry(&manager));
    assert(installed_count == 2U && install_calls == 2U);
}

static void test_failed_root_install_rolls_back_new_dependency(void) {
    reset_fixture();
    fail_editor_install = true;
    struct nexus_app_manager manager;
    make_manager(&manager);
    assert(!nexus_app_manager_install(&manager, "EDITOR"));
    assert(download_calls == 2U);
    assert(installed_count == 0U);
    assert(uninstall_calls == 1U);
    assert(!memory_allocated && !stage_present);
}

int main(void) {
    test_dependency_install_launch_and_remove();
    test_pause_resume_and_retry();
    test_existing_application_updates_in_place();
    test_network_failure_can_retry_from_zero();
    test_failed_root_install_rolls_back_new_dependency();
    puts("NEXUS APP MANAGER TRANSACTION TESTS PASSED");
    return 0;
}
