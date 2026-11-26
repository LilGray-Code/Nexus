#ifndef NEXUS_DOWNLOAD_HTTPS_H
#define NEXUS_DOWNLOAD_HTTPS_H

#include "download_manager.h"
#include "net_https.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_DOWNLOAD_HTTPS_HOST_MAX 128U
#define NEXUS_DOWNLOAD_HTTPS_PATH_MAX 192U
#define NEXUS_DOWNLOAD_HTTPS_HEADER_MAX 2048U
#define NEXUS_DOWNLOAD_HTTPS_TIMEOUT_TICKS 1800U

/*
 * Caller-owned, single-transfer HTTPS state. The current network and NXFS
 * stacks support only one active connection/writer, so this object must stay
 * alive until the download manager closes the source.
 */
struct nexus_download_https_context {
    uint32_t expected_size;
    uint32_t body_remaining;
    struct nexus_https_connection connection;
    char hostname[NEXUS_DOWNLOAD_HTTPS_HOST_MAX + 1U];
    char request_path[NEXUS_DOWNLOAD_HTTPS_PATH_MAX + 1U];
    uint8_t pending[NEXUS_DOWNLOAD_HTTPS_HEADER_MAX];
    size_t pending_offset;
    size_t pending_length;
    bool connected;
};

/* Validate and split an https://host/path URL. Only port 443 is supported. */
bool nexus_download_https_parse_url(
    const char *url,
    char hostname[NEXUS_DOWNLOAD_HTTPS_HOST_MAX + 1U],
    char request_path[NEXUS_DOWNLOAD_HTTPS_PATH_MAX + 1U]);

/* Initialize a stream for one known-size package/blob transfer. */
bool nexus_download_https_context_init(
    struct nexus_download_https_context *context, uint32_t expected_size);

/* Authenticated TLS 1.3 download source for nexus_download_start(). */
const struct nexus_download_source_ops *
nexus_download_https_source_ops(void);

#endif
