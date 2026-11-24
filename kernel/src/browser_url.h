#ifndef NEXUS_BROWSER_URL_H
#define NEXUS_BROWSER_URL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_BROWSER_HOST_MAX 63U

struct nexus_browser_authority {
    char hostname[NEXUS_BROWSER_HOST_MAX + 1U];
    size_t hostname_length;
    uint16_t port;
    bool explicit_port;
};

/* Parse a DNS/IPv4 authority and optional decimal TCP port. IPv6 literals
 * remain unavailable until the kernel network stack supports IPv6.
 */
bool nexus_browser_parse_authority(
    const char *authority, size_t authority_length, uint16_t default_port,
    struct nexus_browser_authority *parsed);

/* Resolve an HTTP Location value against the current HTTP(S) endpoint.
 * HTTPS is never allowed to redirect to plaintext HTTP.
 */
bool nexus_browser_resolve_redirect(
    const char *host, size_t host_length, const char *path,
    size_t path_length, bool current_https, const char *location,
    size_t location_length, char *output, size_t output_capacity,
    size_t *output_length);

#endif
