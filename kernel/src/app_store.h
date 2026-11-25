#ifndef NEXUS_APP_STORE_H
#define NEXUS_APP_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_STORE_URL_MAX 160U
#define NEXUS_STORE_RECORD_MAX 256U
#define NEXUS_STORE_MAX_APPS 10U
#define NEXUS_STORE_NATIVE_PIE_INDEX 9U

enum nexus_store_delivery {
    NEXUS_STORE_DELIVERY_WEB_SHORTCUT = 1,
    NEXUS_STORE_DELIVERY_NATIVE_CLIENT
};

struct nexus_store_application {
    const char *id;
    const char *name;
    const char *publisher;
    const char *category;
    const char *description;
    const char *url;
    enum nexus_store_delivery delivery;
    const char *native_status;
    const char *native_filename;
};

/* Catalog entries are first-party metadata. URLs are fixed HTTPS origins. */
size_t nexus_store_count(void);
const struct nexus_store_application *nexus_store_application_at(size_t index);
bool nexus_store_is_installed(size_t index);
bool nexus_store_install_shortcut(size_t index);
bool nexus_store_uninstall_shortcut(size_t index);
bool nexus_store_copy_url(size_t index, char *output, size_t capacity);
/* Native system-pack entries launch as root-scoped Nexus ELF applications. */
bool nexus_store_launch_native(size_t index, uint64_t *exit_status);

#endif
