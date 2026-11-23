#ifndef NEXUS_LAUNCHER_H
#define NEXUS_LAUNCHER_H

#include <stddef.h>

/* Returns up to capacity indices whose names contain query (ASCII,
 * case-insensitive). Empty query returns every non-null app name. */
size_t nexus_launcher_filter(const char *query,
                             const char *const *app_names,
                             size_t app_count,
                             size_t *results,
                             size_t capacity);

#endif
