#ifndef NEXUS_APP_MANAGER_H
#define NEXUS_APP_MANAGER_H

#include "app_repository.h"
#include "download_https.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum nexus_app_manager_state {
    NEXUS_APP_MANAGER_IDLE = 0,
    NEXUS_APP_MANAGER_INSTALLING,
    NEXUS_APP_MANAGER_PAUSED,
    NEXUS_APP_MANAGER_COMPLETE,
    NEXUS_APP_MANAGER_FAILED,
    NEXUS_APP_MANAGER_CANCELLED
};

enum nexus_app_manager_error {
    NEXUS_APP_MANAGER_ERROR_NONE = 0,
    NEXUS_APP_MANAGER_ERROR_ARGUMENT,
    NEXUS_APP_MANAGER_ERROR_REPOSITORY,
    NEXUS_APP_MANAGER_ERROR_NOT_FOUND,
    NEXUS_APP_MANAGER_ERROR_DEPENDENCY,
    NEXUS_APP_MANAGER_ERROR_DOWNLOAD,
    NEXUS_APP_MANAGER_ERROR_PACKAGE,
    NEXUS_APP_MANAGER_ERROR_PACKAGE_IDENTITY,
    NEXUS_APP_MANAGER_ERROR_PACKAGE_TOO_LARGE,
    NEXUS_APP_MANAGER_ERROR_MEMORY,
    NEXUS_APP_MANAGER_ERROR_STORAGE
};

typedef enum nexus_download_control (*nexus_app_manager_progress_fn)(
    const struct nexus_download_status *status, void *context);

struct nexus_app_manager_status {
    enum nexus_app_manager_state state;
    enum nexus_app_manager_error error;
    uint8_t completed_steps;
    uint8_t total_steps;
    char current_app_id[NEXUS_PACKAGE_ID_MAX + 1U];
    struct nexus_download_status download;
};

/*
 * Persistent caller-owned manager. Keep it alive while a download is paused;
 * the HTTPS transport context is retained across resume/retry.
 */
struct nexus_app_manager {
    struct nexus_app_repository repository;
    nexus_package_signature_verifier package_verifier;
    void *verifier_context;
    nexus_app_manager_progress_fn progress;
    void *progress_context;
    struct nexus_download_https_context https;
    struct nexus_app_install_plan plan;
    uint8_t current_step;
    uint8_t newly_installed_count;
    char newly_installed[NEXUS_APP_REPOSITORY_MAX_APPS]
                        [NEXUS_PACKAGE_ID_MAX + 1U];
    struct nexus_app_manager_status status;
};

/*
 * Parse and authenticate the repository before storing any metadata. Both
 * catalog and package verification are mandatory and can use the same pinned
 * Ed25519 keyring callback.
 */
bool nexus_app_manager_init(
    struct nexus_app_manager *manager,
    const void *signed_repository, size_t repository_length,
    nexus_package_signature_verifier repository_verifier,
    nexus_package_signature_verifier package_verifier,
    void *verifier_context,
    nexus_app_manager_progress_fn progress, void *progress_context);

/* Search the authenticated catalogue by text and exact category. */
size_t nexus_app_manager_search(
    const struct nexus_app_manager *manager,
    const char *query, const char *category,
    uint8_t *result_indices, size_t result_capacity);

/*
 * Install or update a catalogue app and any missing/outdated dependencies.
 * This is a synchronous kernel service with resumable transfer states.
 */
bool nexus_app_manager_install(struct nexus_app_manager *manager,
                               const char *app_id);
bool nexus_app_manager_resume(struct nexus_app_manager *manager);
bool nexus_app_manager_retry(struct nexus_app_manager *manager);
bool nexus_app_manager_cancel(struct nexus_app_manager *manager);

/* Launch and remove a verified native Nexus package. */
bool nexus_app_manager_launch(struct nexus_app_manager *manager,
                              const char *app_id, uint64_t *exit_status);
bool nexus_app_manager_remove(struct nexus_app_manager *manager,
                              const char *app_id);

bool nexus_app_manager_get_status(
    const struct nexus_app_manager *manager,
    struct nexus_app_manager_status *status);

#endif
