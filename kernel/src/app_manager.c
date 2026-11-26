#include "app_manager.h"

#include "memory.h"
#include "package.h"
#include "process.h"
#include "sha256.h"
#include "vfs.h"

#include <stddef.h>
#include <stdint.h>

#define APP_MANAGER_STAGE_PREFIX "DL-"
#define APP_MANAGER_STAGE_SUFFIX ".NUP"
#define APP_MANAGER_RECEIPT_PREFIX "APPS/"
#define APP_MANAGER_RECEIPT_SUFFIX ".NPK"
#define APP_MANAGER_MAX_PACKAGE_BYTES \
    (NEXUS_PACKAGE_MAX_PAYLOAD + NEXUS_PACKAGE_HEADER_MAX)

static bool string_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL) return false;
    size_t i = 0U;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return left[i] == right[i];
}

static bool make_stage_path(const char *id, char path[32]) {
    if (id == NULL || path == NULL || id[0] == '\0') return false;
    size_t out = 0U;
    const char *parts[] = {APP_MANAGER_STAGE_PREFIX, id,
                           APP_MANAGER_STAGE_SUFFIX};
    for (size_t part = 0U; part < sizeof(parts) / sizeof(parts[0]); ++part) {
        for (size_t i = 0U; parts[part][i] != '\0'; ++i) {
            if (out >= 31U) return false;
            path[out++] = parts[part][i];
        }
    }
    path[out] = '\0';
    return true;
}

static bool make_receipt_path(const char *id, char path[32]) {
    if (id == NULL || path == NULL || id[0] == '\0') return false;
    size_t out = 0U;
    const char *parts[] = {APP_MANAGER_RECEIPT_PREFIX, id,
                           APP_MANAGER_RECEIPT_SUFFIX};
    for (size_t part = 0U; part < sizeof(parts) / sizeof(parts[0]); ++part) {
        for (size_t i = 0U; parts[part][i] != '\0'; ++i) {
            if (out >= 31U) return false;
            path[out++] = parts[part][i];
        }
    }
    path[out] = '\0';
    return true;
}

static enum nexus_app_installed_lookup installed_version(
    const char *app_id, char *version, size_t version_capacity,
    void *opaque) {
    struct nexus_app_manager *manager = (struct nexus_app_manager *)opaque;
    if (manager == NULL || app_id == NULL || version == NULL ||
        version_capacity == 0U) return NEXUS_APP_INSTALLED_LOOKUP_ERROR;

    struct nexus_package_install_result installed;
    if (nexus_package_is_installed(app_id, manager->package_verifier,
                                   manager->verifier_context, &installed)) {
        size_t length = 0U;
        while (installed.version[length] != '\0') ++length;
        if (length >= version_capacity)
            return NEXUS_APP_INSTALLED_LOOKUP_ERROR;
        for (size_t i = 0U; i <= length; ++i)
            version[i] = installed.version[i];
        return NEXUS_APP_INSTALLED;
    }

    /* A broken/untrusted receipt is not the same as an absent installation. */
    char receipt[32];
    if (!make_receipt_path(app_id, receipt))
        return NEXUS_APP_INSTALLED_LOOKUP_ERROR;
    struct nexus_vfs_file file;
    if (vfs_open_root_readonly_path(receipt, &file)) {
        vfs_close(&file);
        return NEXUS_APP_INSTALLED_LOOKUP_ERROR;
    }
    return NEXUS_APP_NOT_INSTALLED;
}

static enum nexus_download_control manager_progress(
    const struct nexus_download_status *download, void *opaque) {
    struct nexus_app_manager *manager = (struct nexus_app_manager *)opaque;
    if (manager == NULL) return NEXUS_DOWNLOAD_CANCEL;
    if (manager->progress == NULL) return NEXUS_DOWNLOAD_CONTINUE;
    return manager->progress(download, manager->progress_context);
}

static void remove_stage(struct nexus_app_manager *manager,
                         const char *app_id) {
    (void)manager;
    char path[32];
    if (make_stage_path(app_id, path)) (void)vfs_remove_root_path(path);
}

static void rollback_new_installs(struct nexus_app_manager *manager) {
    while (manager->newly_installed_count != 0U) {
        const uint8_t index = --manager->newly_installed_count;
        (void)nexus_package_uninstall(
            manager->newly_installed[index], manager->package_verifier,
            manager->verifier_context);
        manager->newly_installed[index][0] = '\0';
    }
}

static void set_failure(struct nexus_app_manager *manager,
                        enum nexus_app_manager_error error,
                        bool rollback) {
    manager->status.state = NEXUS_APP_MANAGER_FAILED;
    manager->status.error = error;
    if (rollback) rollback_new_installs(manager);
}

static bool sha256_equal(const uint8_t left[32], const uint8_t right[32]) {
    uint8_t difference = 0U;
    for (size_t i = 0U; i < 32U; ++i)
        difference |= (uint8_t)(left[i] ^ right[i]);
    return difference == 0U;
}

static bool install_staged_package(struct nexus_app_manager *manager,
                                   const struct nexus_app_repository_entry *app) {
    char stage_path[32];
    if (!make_stage_path(app->id, stage_path)) {
        set_failure(manager, NEXUS_APP_MANAGER_ERROR_ARGUMENT, true);
        return false;
    }

    struct nexus_vfs_file file;
    if (!vfs_open_root_readonly_path(stage_path, &file)) {
        set_failure(manager, NEXUS_APP_MANAGER_ERROR_STORAGE, true);
        return false;
    }

    uint32_t file_size = 0U;
    if (!vfs_file_size(&file, &file_size) ||
        file_size != app->package_size || file_size == 0U ||
        file_size > APP_MANAGER_MAX_PACKAGE_BYTES) {
        vfs_close(&file);
        remove_stage(manager, app->id);
        set_failure(manager, NEXUS_APP_MANAGER_ERROR_PACKAGE_TOO_LARGE, true);
        return false;
    }

    const uint64_t pages =
        ((uint64_t)file_size + NEXUS_PAGE_SIZE - 1U) / NEXUS_PAGE_SIZE;
    uint64_t physical = 0U;
    if (pages == 0U ||
        !memory_alloc_contiguous_pages(pages, 0U, &physical)) {
        vfs_close(&file);
        remove_stage(manager, app->id);
        set_failure(manager, NEXUS_APP_MANAGER_ERROR_MEMORY, true);
        return false;
    }
    uint8_t *package_bytes = (uint8_t *)memory_phys_to_virt(physical);
    if (package_bytes == NULL) {
        (void)memory_free_contiguous_pages(physical, pages);
        vfs_close(&file);
        remove_stage(manager, app->id);
        set_failure(manager, NEXUS_APP_MANAGER_ERROR_MEMORY, true);
        return false;
    }

    uint32_t offset = 0U;
    bool read_ok = true;
    while (offset < file_size) {
        size_t amount = file_size - offset;
        if (amount > NEXUS_VFS_READ_CHUNK) amount = NEXUS_VFS_READ_CHUNK;
        size_t received = 0U;
        if (!vfs_read(&file, package_bytes + offset, amount, &received) ||
            received != amount) {
            read_ok = false;
            break;
        }
        offset += (uint32_t)received;
    }
    vfs_close(&file);

    bool installed_ok = false;
    enum nexus_app_manager_error install_error = NEXUS_APP_MANAGER_ERROR_PACKAGE;
    if (read_ok) {
        uint8_t digest[32];
        nexus_sha256(package_bytes, file_size, digest);
        if (sha256_equal(digest, app->package_sha256)) {
            struct nexus_package_manifest manifest;
            if (nexus_package_verify(package_bytes, file_size,
                    manager->package_verifier, manager->verifier_context,
                    &manifest) &&
                string_equal(manifest.id, app->id) &&
                string_equal(manifest.version, app->version) &&
                manifest.payload_size <= NEXUS_PROCESS_MAX_IMAGE_BYTES) {
                struct nexus_package_install_result result;
                if (nexus_package_install(package_bytes, file_size,
                        manager->package_verifier, manager->verifier_context,
                        &result) &&
                    string_equal(result.id, app->id) &&
                    string_equal(result.version, app->version)) {
                    installed_ok = true;
                } else {
                    install_error = NEXUS_APP_MANAGER_ERROR_PACKAGE;
                }
            } else {
                install_error = NEXUS_APP_MANAGER_ERROR_PACKAGE_IDENTITY;
            }
        } else {
            install_error = NEXUS_APP_MANAGER_ERROR_PACKAGE;
        }
        for (uint32_t i = 0U; i < file_size; ++i) package_bytes[i] = 0U;
    }

    const bool memory_released =
        memory_free_contiguous_pages(physical, pages);
    remove_stage(manager, app->id);
    if (!read_ok || !memory_released || !installed_ok) {
        set_failure(manager, !read_ok || !memory_released
            ? NEXUS_APP_MANAGER_ERROR_STORAGE : install_error, true);
        return false;
    }

    if (!manager->plan.steps[manager->current_step].is_update &&
        manager->newly_installed_count < NEXUS_APP_REPOSITORY_MAX_APPS) {
        const uint8_t index = manager->newly_installed_count++;
        for (size_t i = 0U; i < sizeof(manager->newly_installed[index]); ++i)
            manager->newly_installed[index][i] = app->id[i];
    }
    return true;
}

static bool advance_plan(struct nexus_app_manager *manager);

static bool finish_current_download(struct nexus_app_manager *manager) {
    const uint8_t repo_index =
        manager->plan.steps[manager->current_step].repository_index;
    const struct nexus_app_repository_entry *app =
        &manager->repository.apps[repo_index];
    if (!install_staged_package(manager, app)) return false;

    if (manager->current_step < manager->plan.step_count)
        ++manager->current_step;
    manager->status.completed_steps = manager->current_step;
    manager->status.current_app_id[0] = '\0';
    return advance_plan(manager);
}

static bool advance_plan(struct nexus_app_manager *manager) {
    while (manager->current_step < manager->plan.step_count) {
        const uint8_t repo_index =
            manager->plan.steps[manager->current_step].repository_index;
        const struct nexus_app_repository_entry *app =
            &manager->repository.apps[repo_index];

        for (size_t i = 0U; i < sizeof(manager->status.current_app_id); ++i)
            manager->status.current_app_id[i] = app->id[i];

        if (app->package_size > APP_MANAGER_MAX_PACKAGE_BYTES) {
            set_failure(manager, NEXUS_APP_MANAGER_ERROR_PACKAGE_TOO_LARGE,
                        true);
            return false;
        }
        char stage_path[32];
        if (!make_stage_path(app->id, stage_path) ||
            !nexus_download_https_context_init(&manager->https,
                                                app->package_size)) {
            set_failure(manager, NEXUS_APP_MANAGER_ERROR_ARGUMENT, true);
            return false;
        }

        const bool started = nexus_download_start(
            app->package_url, stage_path, app->package_size,
            app->package_sha256, nexus_download_https_source_ops(),
            &manager->https, manager_progress, manager);
        struct nexus_download_status download = {0};
        if (!nexus_download_get_status(&download)) {
            set_failure(manager, NEXUS_APP_MANAGER_ERROR_DOWNLOAD, true);
            return false;
        }
        manager->status.download = download;
        if (download.state == NEXUS_DOWNLOAD_PAUSED) {
            manager->status.state = NEXUS_APP_MANAGER_PAUSED;
            return true;
        }
        if (download.state != NEXUS_DOWNLOAD_COMPLETE || !started) {
            set_failure(manager, NEXUS_APP_MANAGER_ERROR_DOWNLOAD, false);
            return false; /* Keep the dependency plan available for retry. */
        }
        if (!finish_current_download(manager)) return false;
    }

    manager->status.state = NEXUS_APP_MANAGER_COMPLETE;
    manager->status.error = NEXUS_APP_MANAGER_ERROR_NONE;
    manager->status.current_app_id[0] = '\0';
    return true;
}

bool nexus_app_manager_init(
    struct nexus_app_manager *manager,
    const void *signed_repository, size_t repository_length,
    nexus_package_signature_verifier repository_verifier,
    nexus_package_signature_verifier package_verifier,
    void *verifier_context,
    nexus_app_manager_progress_fn progress, void *progress_context) {
    if (manager == NULL || signed_repository == NULL ||
        repository_verifier == NULL || package_verifier == NULL) return false;
    *manager = (struct nexus_app_manager){0};
    if (!nexus_app_repository_parse_verify(
            signed_repository, repository_length, repository_verifier,
            verifier_context, &manager->repository)) {
        manager->status.state = NEXUS_APP_MANAGER_FAILED;
        manager->status.error = NEXUS_APP_MANAGER_ERROR_REPOSITORY;
        return false;
    }
    manager->package_verifier = package_verifier;
    manager->verifier_context = verifier_context;
    manager->progress = progress;
    manager->progress_context = progress_context;
    manager->status.state = NEXUS_APP_MANAGER_IDLE;
    return true;
}

size_t nexus_app_manager_search(
    const struct nexus_app_manager *manager,
    const char *query, const char *category,
    uint8_t *result_indices, size_t result_capacity) {
    if (manager == NULL) return 0U;
    return nexus_app_repository_search(
        &manager->repository, query, category, result_indices, result_capacity);
}

bool nexus_app_manager_install(struct nexus_app_manager *manager,
                               const char *app_id) {
    if (manager == NULL || app_id == NULL ||
        (manager->status.state == NEXUS_APP_MANAGER_INSTALLING) ||
        (manager->status.state == NEXUS_APP_MANAGER_PAUSED)) return false;
    if (!nexus_app_repository_plan_install(
            &manager->repository, app_id, installed_version, manager,
            &manager->plan)) {
        manager->status.state = NEXUS_APP_MANAGER_FAILED;
        manager->status.error = NEXUS_APP_MANAGER_ERROR_DEPENDENCY;
        return false;
    }
    manager->current_step = 0U;
    manager->newly_installed_count = 0U;
    manager->status.state = NEXUS_APP_MANAGER_INSTALLING;
    manager->status.error = NEXUS_APP_MANAGER_ERROR_NONE;
    manager->status.completed_steps = 0U;
    manager->status.total_steps = manager->plan.step_count;
    manager->status.current_app_id[0] = '\0';
    if (manager->plan.step_count == 0U) {
        manager->status.state = NEXUS_APP_MANAGER_COMPLETE;
        return true;
    }
    return advance_plan(manager);
}

bool nexus_app_manager_resume(struct nexus_app_manager *manager) {
    if (manager == NULL ||
        manager->status.state != NEXUS_APP_MANAGER_PAUSED) return false;
    const bool resumed = nexus_download_resume(manager_progress, manager);
    struct nexus_download_status download = {0};
    if (!nexus_download_get_status(&download)) {
        set_failure(manager, NEXUS_APP_MANAGER_ERROR_DOWNLOAD, true);
        return false;
    }
    manager->status.download = download;
    if (download.state == NEXUS_DOWNLOAD_PAUSED) return true;
    if (!resumed || download.state != NEXUS_DOWNLOAD_COMPLETE) {
        set_failure(manager, NEXUS_APP_MANAGER_ERROR_DOWNLOAD, false);
        return false;
    }
    manager->status.state = NEXUS_APP_MANAGER_INSTALLING;
    return finish_current_download(manager);
}

bool nexus_app_manager_retry(struct nexus_app_manager *manager) {
    if (manager == NULL ||
        manager->status.state != NEXUS_APP_MANAGER_FAILED ||
        manager->status.error != NEXUS_APP_MANAGER_ERROR_DOWNLOAD) return false;
    const bool retried = nexus_download_retry(manager_progress, manager);
    struct nexus_download_status download = {0};
    if (!nexus_download_get_status(&download)) return false;
    manager->status.download = download;
    if (download.state == NEXUS_DOWNLOAD_PAUSED) {
        manager->status.state = NEXUS_APP_MANAGER_PAUSED;
        return true;
    }
    if (!retried || download.state != NEXUS_DOWNLOAD_COMPLETE) {
        manager->status.error = NEXUS_APP_MANAGER_ERROR_DOWNLOAD;
        return false;
    }
    manager->status.state = NEXUS_APP_MANAGER_INSTALLING;
    return finish_current_download(manager);
}

bool nexus_app_manager_cancel(struct nexus_app_manager *manager) {
    if (manager == NULL ||
        (manager->status.state != NEXUS_APP_MANAGER_PAUSED &&
         manager->status.state != NEXUS_APP_MANAGER_FAILED)) return false;
    (void)nexus_download_cancel();
    if (manager->status.current_app_id[0] != '\0')
        remove_stage(manager, manager->status.current_app_id);
    rollback_new_installs(manager);
    manager->status.state = NEXUS_APP_MANAGER_CANCELLED;
    manager->status.error = NEXUS_APP_MANAGER_ERROR_NONE;
    manager->status.current_app_id[0] = '\0';
    return true;
}

bool nexus_app_manager_launch(struct nexus_app_manager *manager,
                              const char *app_id, uint64_t *exit_status) {
    if (manager == NULL || app_id == NULL ||
        manager->package_verifier == NULL) return false;
    return nexus_package_launch(app_id, manager->package_verifier,
                                manager->verifier_context, exit_status);
}

bool nexus_app_manager_remove(struct nexus_app_manager *manager,
                              const char *app_id) {
    if (manager == NULL || app_id == NULL ||
        manager->package_verifier == NULL) return false;
    if (installed_version(app_id, (char[NEXUS_PACKAGE_VERSION_MAX + 1U]){0},
            NEXUS_PACKAGE_VERSION_MAX + 1U, manager) != NEXUS_APP_INSTALLED) {
        manager->status.error = NEXUS_APP_MANAGER_ERROR_PACKAGE;
        return false;
    }

    /* Refuse removal while any other installed catalogue app depends on it. */
    for (uint32_t i = 0U; i < manager->repository.app_count; ++i) {
        const struct nexus_app_repository_entry *candidate =
            &manager->repository.apps[i];
        if (string_equal(candidate->id, app_id)) continue;
        bool depends_on_target = false;
        for (uint8_t d = 0U; d < candidate->dependency_count; ++d)
            if (string_equal(candidate->dependencies[d].id, app_id))
                depends_on_target = true;
        if (!depends_on_target) continue;
        char installed_version_text[NEXUS_PACKAGE_VERSION_MAX + 1U] = {0};
        if (installed_version(candidate->id, installed_version_text,
                sizeof(installed_version_text), manager) ==
                NEXUS_APP_INSTALLED) {
            manager->status.error = NEXUS_APP_MANAGER_ERROR_DEPENDENCY;
            return false;
        }
    }

    if (!nexus_package_uninstall(app_id, manager->package_verifier,
                                 manager->verifier_context)) {
        manager->status.error = NEXUS_APP_MANAGER_ERROR_PACKAGE;
        return false;
    }
    manager->status.error = NEXUS_APP_MANAGER_ERROR_NONE;
    return true;
}

bool nexus_app_manager_get_status(
    const struct nexus_app_manager *manager,
    struct nexus_app_manager_status *status) {
    if (manager == NULL || status == NULL) return false;
    *status = manager->status;
    (void)nexus_download_get_status(&status->download);
    return true;
}
