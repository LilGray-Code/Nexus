#ifndef NEXUS_DOWNLOAD_MANAGER_H
#define NEXUS_DOWNLOAD_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_DOWNLOAD_MANAGER_MAX_BYTES (256U * 1024U * 1024U)
#define NEXUS_DOWNLOAD_MANAGER_CHUNK_BYTES 4096U
#define NEXUS_DOWNLOAD_MANAGER_NAME_MAX 32U

struct nexus_download_source_ops {
    /* The transport must authenticate HTTPS and enforce the requested URL. */
    bool (*open)(const char *https_url, void **session, void *context);
    /* Returns 1..capacity bytes, zero at end-of-body, or a negative error. */
    int (*read)(void *session, uint8_t *buffer, size_t capacity,
                void *context);
    void (*close)(void *session, void *context);
};

enum nexus_download_state {
    NEXUS_DOWNLOAD_IDLE = 0,
    NEXUS_DOWNLOAD_ACTIVE,
    NEXUS_DOWNLOAD_PAUSED,
    NEXUS_DOWNLOAD_COMPLETE,
    NEXUS_DOWNLOAD_FAILED,
    NEXUS_DOWNLOAD_CANCELLED
};

enum nexus_download_error {
    NEXUS_DOWNLOAD_ERROR_NONE = 0,
    NEXUS_DOWNLOAD_ERROR_ARGUMENT,
    NEXUS_DOWNLOAD_ERROR_NETWORK,
    NEXUS_DOWNLOAD_ERROR_SIZE,
    NEXUS_DOWNLOAD_ERROR_INTEGRITY,
    NEXUS_DOWNLOAD_ERROR_STORAGE,
    NEXUS_DOWNLOAD_ERROR_CANCELLED
};

enum nexus_download_control {
    NEXUS_DOWNLOAD_CONTINUE = 0,
    NEXUS_DOWNLOAD_PAUSE,
    NEXUS_DOWNLOAD_CANCEL
};

struct nexus_download_status {
    enum nexus_download_state state;
    enum nexus_download_error error;
    char destination[NEXUS_DOWNLOAD_MANAGER_NAME_MAX];
    uint32_t bytes_received;
    uint32_t total_bytes;
    uint8_t percent;
};

typedef enum nexus_download_control (*nexus_download_progress_fn)(
    const struct nexus_download_status *status, void *context);

/*
 * One transfer at a time: NXFS and the current TCP/TLS stack each have a
 * single active writer/connection. Data is written in 4 KiB chunks and never
 * buffered as a whole download. A valid repository/package SHA-256 is
 * mandatory. The URL must be HTTPS and the source transport must authenticate
 * its server certificate.
 */
bool nexus_download_start(
    const char *https_url, const char *destination,
    uint32_t total_bytes,
    const uint8_t expected_sha256[32],
    const struct nexus_download_source_ops *source,
    void *source_context,
    nexus_download_progress_fn progress, void *progress_context);

/* Resume a paused transfer on its still-open source connection. */
bool nexus_download_resume(nexus_download_progress_fn progress,
                           void *progress_context);

/* Retry a failed transfer from byte zero using a new source connection. */
bool nexus_download_retry(nexus_download_progress_fn progress,
                          void *progress_context);

/* Cancel an active or paused transfer and discard the temporary NXFS write. */
bool nexus_download_cancel(void);

bool nexus_download_get_status(struct nexus_download_status *status);

#endif
