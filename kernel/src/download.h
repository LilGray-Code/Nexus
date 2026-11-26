#ifndef NEXUS_DOWNLOAD_H
#define NEXUS_DOWNLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_DOWNLOAD_NAME_MAX 32U
#define NEXUS_DOWNLOAD_MAX_BYTES (256U * 1024U * 1024U)

/*
 * Build a safe, flat VFS filename from Content-Disposition or the URL path.
 * Only a single leaf name is returned; directory separators and control
 * characters are never allowed through this boundary.
 */
bool nexus_download_filename(const char *url,
                             const char *content_disposition,
                             const char *content_type,
                             char output[NEXUS_DOWNLOAD_NAME_MAX]);

/* Add a collision suffix before the extension: APP.ZIP -> APP_1.ZIP. */
bool nexus_download_unique_name(const char *base_name, uint32_t sequence,
                                char output[NEXUS_DOWNLOAD_NAME_MAX]);

#endif
