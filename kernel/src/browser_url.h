#ifndef NEXUS_BROWSER_URL_H
#define NEXUS_BROWSER_URL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_BROWSER_HOST_MAX 253U
#define NEXUS_BROWSER_PATH_MAX 1023U

enum nexus_browser_scheme {
    NEXUS_BROWSER_SCHEME_HTTP = 1,
    NEXUS_BROWSER_SCHEME_HTTPS = 2
};

struct nexus_browser_url {
    enum nexus_browser_scheme scheme;
    char host[NEXUS_BROWSER_HOST_MAX + 1U];
    char path[NEXUS_BROWSER_PATH_MAX + 1U];
    uint16_t port;
    bool explicit_port;
};

/* Strict absolute URL parser. HTTPS is parsed for navigation modeling only;
 * the current network stack does not implement TLS and must not fetch it. */
bool nexus_browser_url_parse(const char *input,
                             struct nexus_browser_url *output);

#endif
