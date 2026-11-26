#ifndef NEXUS_APP_REPOSITORY_H
#define NEXUS_APP_REPOSITORY_H

#include "package.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_APP_REPOSITORY_MAX_APPS 32U
#define NEXUS_APP_REPOSITORY_MAX_BYTES 16384U
#define NEXUS_APP_REPOSITORY_MAX_NAME 40U
#define NEXUS_APP_REPOSITORY_MAX_PUBLISHER 32U
#define NEXUS_APP_REPOSITORY_MAX_CATEGORY 20U
#define NEXUS_APP_REPOSITORY_MAX_DESCRIPTION 120U
#define NEXUS_APP_REPOSITORY_MAX_DEPENDENCIES 4U
#define NEXUS_APP_REPOSITORY_MAX_URL 159U
#define NEXUS_APP_REPOSITORY_MAX_LINE 512U

struct nexus_app_dependency {
    char id[NEXUS_PACKAGE_ID_MAX + 1U];
    char minimum_version[NEXUS_PACKAGE_VERSION_MAX + 1U];
};

struct nexus_app_repository_entry {
    char id[NEXUS_PACKAGE_ID_MAX + 1U];
    char version[NEXUS_PACKAGE_VERSION_MAX + 1U];
    char name[NEXUS_APP_REPOSITORY_MAX_NAME + 1U];
    char publisher[NEXUS_APP_REPOSITORY_MAX_PUBLISHER + 1U];
    char category[NEXUS_APP_REPOSITORY_MAX_CATEGORY + 1U];
    char description[NEXUS_APP_REPOSITORY_MAX_DESCRIPTION + 1U];
    char package_url[NEXUS_APP_REPOSITORY_MAX_URL + 1U];
    char screenshot_url[NEXUS_APP_REPOSITORY_MAX_URL + 1U];
    uint32_t package_size;
    uint32_t rating_count;
    uint8_t rating_tenths;
    uint8_t dependency_count;
    uint8_t package_sha256[NEXUS_PACKAGE_DIGEST_BYTES];
    struct nexus_app_dependency dependencies[
        NEXUS_APP_REPOSITORY_MAX_DEPENDENCIES];
};

struct nexus_app_repository {
    uint32_t generation;
    uint32_t app_count;
    char key_id[NEXUS_PACKAGE_KEY_ID_MAX + 1U];
    struct nexus_app_repository_entry apps[
        NEXUS_APP_REPOSITORY_MAX_APPS];
};

enum nexus_app_installed_lookup {
    NEXUS_APP_NOT_INSTALLED = 0,
    NEXUS_APP_INSTALLED,
    NEXUS_APP_INSTALLED_LOOKUP_ERROR
};

typedef enum nexus_app_installed_lookup (*nexus_app_installed_version_fn)(
    const char *app_id, char *version, size_t version_capacity,
    void *context);

struct nexus_app_install_step {
    uint8_t repository_index;
    bool is_update;
};

struct nexus_app_install_plan {
    uint8_t step_count;
    struct nexus_app_install_step steps[NEXUS_APP_REPOSITORY_MAX_APPS];
};

/* Verify a signed repository catalog before exposing any of its metadata. */
bool nexus_app_repository_parse_verify(
    const void *bytes, size_t length,
    nexus_package_signature_verifier verifier, void *verifier_context,
    struct nexus_app_repository *repository);

/* Exact, uppercase package ID lookup; returns -1 when not present. */
int nexus_app_repository_find(const struct nexus_app_repository *repository,
                              const char *app_id);

/*
 * Search by ID, name, publisher or description. An empty query matches all
 * entries. A non-empty category is an exact case-insensitive category filter.
 * Returns the number of indices written, bounded by result_capacity.
 */
size_t nexus_app_repository_search(
    const struct nexus_app_repository *repository,
    const char *query, const char *category,
    uint8_t *result_indices, size_t result_capacity);

/* Compare validated three-component Nexus versions. */
bool nexus_app_version_compare(const char *left, const char *right,
                               int *comparison);

/*
 * Resolve dependencies before the requested application. Already-installed
 * dependencies meeting their minimum version are left untouched. The root
 * application is upgraded to the repository version when it is older.
 */
bool nexus_app_repository_plan_install(
    const struct nexus_app_repository *repository, const char *app_id,
    nexus_app_installed_version_fn installed_version, void *context,
    struct nexus_app_install_plan *plan);

#endif
