#ifndef NEXUS_DOWNLOAD_H
#define NEXUS_DOWNLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sha256.h"

#define NEXUS_DOWNLOAD_NAME_MAX 32U
#define NEXUS_DOWNLOAD_MAX_BYTES (256U * 1024U * 1024U)

enum nexus_download_job_state {
    NEXUS_DOWNLOAD_JOB_IDLE = 0,
    NEXUS_DOWNLOAD_JOB_ACTIVE,
    NEXUS_DOWNLOAD_JOB_COMPLETE,
    NEXUS_DOWNLOAD_JOB_CANCELLED,
    NEXUS_DOWNLOAD_JOB_FAILED
};

struct nexus_download_job {
    enum nexus_download_job_state state;
    uint32_t maximum_bytes;
    uint32_t expected_bytes;
    uint32_t received_bytes;
    bool length_known;
    bool has_expected_sha256;
    bool sha256_valid;
    bool checksum_verified;
    uint8_t expected_sha256[NEXUS_SHA256_SIZE];
    uint8_t actual_sha256[NEXUS_SHA256_SIZE];
    struct nexus_sha256_context hash;
};

/*
 * Initialize a bounded streaming transfer. max_bytes should come from the
 * mounted filesystem's per-file capability, not a UI buffer size. A supplied
 * digest is verified at finish; without one, the local digest is still saved
 * in the job for display or a sidecar record.
 */
bool nexus_download_job_begin(struct nexus_download_job *job,
                              uint32_t max_bytes, bool length_known,
                              uint32_t expected_bytes,
                              const uint8_t expected_sha256[NEXUS_SHA256_SIZE]);
bool nexus_download_job_append(struct nexus_download_job *job,
                               const void *data, size_t length);
bool nexus_download_job_finish(struct nexus_download_job *job);
void nexus_download_job_cancel(struct nexus_download_job *job);
void nexus_download_job_fail(struct nexus_download_job *job);
/* Returns 0..100 for known lengths, UINT32_MAX while total size is unknown. */
uint32_t nexus_download_job_progress_percent(
    const struct nexus_download_job *job);
bool nexus_download_sha256_hex(
    const uint8_t digest[NEXUS_SHA256_SIZE], char output[65]);

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
