#ifndef NEXUS_BROWSER_URL_H
#define NEXUS_BROWSER_URL_H

#include <stdbool.h>
#include <stddef.h>

/* Resolve an HTTP Location value against the current HTTP(S) endpoint.
 * HTTPS is never allowed to redirect to plaintext HTTP.
 */
bool nexus_browser_resolve_redirect(
    const char *host, size_t host_length, const char *path,
    size_t path_length, bool current_https, const char *location,
    size_t location_length, char *output, size_t output_capacity,
    size_t *output_length);

#endif
