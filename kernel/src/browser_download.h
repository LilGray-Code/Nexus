#ifndef NEXUS_BROWSER_DOWNLOAD_H
#define NEXUS_BROWSER_DOWNLOAD_H

#include <stddef.h>

enum nexus_browser_download_result {
    NEXUS_BROWSER_DOWNLOAD_OK = 0,
    NEXUS_BROWSER_DOWNLOAD_INVALID_URL,
    NEXUS_BROWSER_DOWNLOAD_HTTPS_BLOCKED,
    NEXUS_BROWSER_DOWNLOAD_DNS_FAILED,
    NEXUS_BROWSER_DOWNLOAD_CONNECT_FAILED,
    NEXUS_BROWSER_DOWNLOAD_REQUEST_FAILED,
    NEXUS_BROWSER_DOWNLOAD_HEADER_FAILED,
    NEXUS_BROWSER_DOWNLOAD_REDIRECT,
    NEXUS_BROWSER_DOWNLOAD_CHUNKED_UNSUPPORTED,
    NEXUS_BROWSER_DOWNLOAD_LENGTH_REQUIRED,
    NEXUS_BROWSER_DOWNLOAD_TOO_LARGE,
    NEXUS_BROWSER_DOWNLOAD_INCOMPLETE,
    NEXUS_BROWSER_DOWNLOAD_NOT_PERSISTENT,
    NEXUS_BROWSER_DOWNLOAD_FILE_EXISTS,
    NEXUS_BROWSER_DOWNLOAD_SAVE_FAILED,
    NEXUS_BROWSER_DOWNLOAD_VERIFY_FAILED
};

/*
 * Download one HTTP resource into the persistent Nexus Data volume.
 * HTTPS is rejected until a TLS client and certificate validator exist.
 * Existing names are never overwritten. The saved file is read back and
 * byte-compared before success is returned.
 */
enum nexus_browser_download_result
nexus_browser_download_http(const char *url, char *saved_name,
                            size_t saved_name_capacity);

#endif
