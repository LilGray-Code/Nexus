#include "download_manager.h"

#include "nxfs.h"
#include "sha256.h"

#include <stddef.h>
#include <stdint.h>

struct download_session {
    struct nexus_download_status status;
    char url[NEXUS_DOWNLOAD_MANAGER_NAME_MAX * 6U];
    uint8_t expected_sha256[32];
    struct nexus_sha256_context hash;
    const struct nexus_download_source_ops *source;
    void *source_context;
    void *source_session;
    bool writer_open;
};

static struct download_session active_download;

static bool copy_text(char *out, size_t capacity, const char *in) {
    if (out == NULL || in == NULL || capacity == 0U) return false;
    size_t length = 0U;
    while (in[length] != '\0') {
        if (length + 1U >= capacity) return false;
        out[length] = in[length];
        ++length;
    }
    out[length] = '\0';
    return length != 0U;
}

static bool url_valid(const char *url) {
    static const char scheme[] = "https://";
    if (url == NULL) return false;
    for (size_t i = 0U; i < sizeof(scheme) - 1U; ++i)
        if (url[i] != scheme[i]) return false;
    size_t host = sizeof(scheme) - 1U, cursor = host;
    while (url[cursor] != '\0' && url[cursor] != '/' &&
           url[cursor] != '?' && url[cursor] != '#') {
        const char c = url[cursor];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
        ++cursor;
        if (cursor - host > 100U) return false;
    }
    if (cursor == host || url[host] == '.' || url[host] == '-' ||
        url[cursor - 1U] == '.' || url[cursor - 1U] == '-') return false;
    size_t length = cursor;
    while (url[length] != '\0') {
        const unsigned char c = (unsigned char)url[length++];
        if (c <= 0x20U || c >= 0x7FU || c == '\\' || c == '|') return false;
        if (length >= sizeof(active_download.url)) return false;
    }
    return true;
}

static bool destination_valid(const char *name, char output[
                                  NEXUS_DOWNLOAD_MANAGER_NAME_MAX]) {
    if (name == NULL || output == NULL) return false;
    size_t length = 0U;
    while (name[length] != '\0') {
        if (length >= NEXUS_DOWNLOAD_MANAGER_NAME_MAX - 1U) return false;
        const char c = name[length];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' ||
              c == '.' || c == '/')) return false;
        output[length] = c >= 'a' && c <= 'z'
            ? (char)(c - 'a' + 'A') : c;
        ++length;
    }
    if (length == 0U || output[0] == '/' || output[length - 1U] == '/')
        return false;
    output[length] = '\0';
    size_t component = 0U;
    for (size_t i = 0U; i <= length; ++i) {
        if (output[i] == '/' || output[i] == '\0') {
            if (component == 0U ||
                (component == 1U && output[i - 1U] == '.') ||
                (component == 2U && output[i - 2U] == '.' &&
                 output[i - 1U] == '.')) return false;
            component = 0U;
        } else {
            ++component;
        }
    }
    return true;
}

static void source_close(void) {
    if (active_download.source_session != NULL &&
        active_download.source != NULL &&
        active_download.source->close != NULL) {
        active_download.source->close(active_download.source_session,
                                      active_download.source_context);
    }
    active_download.source_session = NULL;
}

static void report(nexus_download_progress_fn progress, void *context) {
    if (progress == NULL) return;
    const uint64_t percent =
        ((uint64_t)active_download.status.bytes_received * 100U) /
        active_download.status.total_bytes;
    active_download.status.percent =
        (uint8_t)(percent > 100U ? 100U : percent);
    (void)progress(&active_download.status, context);
}

static void fail(enum nexus_download_error error) {
    source_close();
    if (active_download.writer_open) {
        nxfs_write_file_abort();
        active_download.writer_open = false;
    }
    active_download.status.state = NEXUS_DOWNLOAD_FAILED;
    active_download.status.error = error;
}

static bool open_and_begin(void) {
    if (active_download.source == NULL ||
        active_download.source->open == NULL ||
        !active_download.source->open(active_download.url,
            &active_download.source_session, active_download.source_context) ||
        active_download.source_session == NULL) {
        fail(NEXUS_DOWNLOAD_ERROR_NETWORK);
        return false;
    }
    if (!nxfs_write_file_begin(active_download.status.destination,
                               active_download.status.total_bytes)) {
        fail(NEXUS_DOWNLOAD_ERROR_STORAGE);
        return false;
    }
    active_download.writer_open = true;
    nexus_sha256_init(&active_download.hash);
    active_download.status.bytes_received = 0U;
    active_download.status.percent = 0U;
    active_download.status.error = NEXUS_DOWNLOAD_ERROR_NONE;
    active_download.status.state = NEXUS_DOWNLOAD_ACTIVE;
    return true;
}

static bool transfer(nexus_download_progress_fn progress,
                     void *progress_context) {
    uint8_t buffer[NEXUS_DOWNLOAD_MANAGER_CHUNK_BYTES];
    while (active_download.status.bytes_received <
           active_download.status.total_bytes) {
        const uint32_t remaining = active_download.status.total_bytes -
            active_download.status.bytes_received;
        size_t capacity = sizeof(buffer);
        if (capacity > remaining) capacity = remaining;
        const int received = active_download.source->read(
            active_download.source_session, buffer, capacity,
            active_download.source_context);
        if (received <= 0) {
            fail(received == 0 ? NEXUS_DOWNLOAD_ERROR_SIZE
                               : NEXUS_DOWNLOAD_ERROR_NETWORK);
            report(progress, progress_context);
            return false;
        }
        if ((size_t)received > capacity ||
            !nxfs_write_file_append(buffer, (size_t)received)) {
            fail((size_t)received > capacity
                ? NEXUS_DOWNLOAD_ERROR_SIZE : NEXUS_DOWNLOAD_ERROR_STORAGE);
            report(progress, progress_context);
            return false;
        }
        nexus_sha256_update(&active_download.hash, buffer, (size_t)received);
        active_download.status.bytes_received += (uint32_t)received;
        const uint64_t percent =
            ((uint64_t)active_download.status.bytes_received * 100U) /
            active_download.status.total_bytes;
        active_download.status.percent =
            (uint8_t)(percent > 100U ? 100U : percent);
        if (progress != NULL) {
            const enum nexus_download_control control =
                progress(&active_download.status, progress_context);
            if (control == NEXUS_DOWNLOAD_PAUSE) {
                active_download.status.state = NEXUS_DOWNLOAD_PAUSED;
                return true;
            }
            if (control == NEXUS_DOWNLOAD_CANCEL) {
                source_close();
                nxfs_write_file_abort();
                active_download.writer_open = false;
                active_download.status.state = NEXUS_DOWNLOAD_CANCELLED;
                active_download.status.error = NEXUS_DOWNLOAD_ERROR_CANCELLED;
                return false;
            }
        }
    }

    uint8_t digest[32];
    nexus_sha256_final(&active_download.hash, digest);
    uint8_t difference = 0U;
    for (size_t i = 0U; i < sizeof(digest); ++i)
        difference |= (uint8_t)(digest[i] ^
                                active_download.expected_sha256[i]);
    for (size_t i = 0U; i < sizeof(digest); ++i) digest[i] = 0U;
    if (difference != 0U) {
        fail(NEXUS_DOWNLOAD_ERROR_INTEGRITY);
        report(progress, progress_context);
        return false;
    }
    if (!nxfs_write_file_finish()) {
        fail(NEXUS_DOWNLOAD_ERROR_STORAGE);
        report(progress, progress_context);
        return false;
    }
    active_download.writer_open = false;
    source_close();
    active_download.status.state = NEXUS_DOWNLOAD_COMPLETE;
    active_download.status.error = NEXUS_DOWNLOAD_ERROR_NONE;
    active_download.status.percent = 100U;
    report(progress, progress_context);
    return true;
}

bool nexus_download_start(
    const char *https_url, const char *destination, uint32_t total_bytes,
    const uint8_t expected_sha256[32],
    const struct nexus_download_source_ops *source, void *source_context,
    nexus_download_progress_fn progress, void *progress_context) {
    if ((active_download.status.state == NEXUS_DOWNLOAD_ACTIVE) ||
        (active_download.status.state == NEXUS_DOWNLOAD_PAUSED)) return false;
    if (!url_valid(https_url) || total_bytes == 0U ||
        total_bytes > NEXUS_DOWNLOAD_MANAGER_MAX_BYTES ||
        expected_sha256 == NULL || source == NULL ||
        source->open == NULL || source->read == NULL ||
        source->close == NULL || !nxfs_is_mounted() ||
        !nxfs_is_writable() || total_bytes > nxfs_get_max_file_bytes()) {
        active_download.status.state = NEXUS_DOWNLOAD_FAILED;
        active_download.status.error = NEXUS_DOWNLOAD_ERROR_ARGUMENT;
        return false;
    }

    struct download_session fresh = {0};
    if (!copy_text(fresh.url, sizeof(fresh.url), https_url) ||
        !destination_valid(destination, fresh.status.destination)) {
        active_download.status.state = NEXUS_DOWNLOAD_FAILED;
        active_download.status.error = NEXUS_DOWNLOAD_ERROR_ARGUMENT;
        return false;
    }
    fresh.status.state = NEXUS_DOWNLOAD_IDLE;
    fresh.status.total_bytes = total_bytes;
    for (size_t i = 0U; i < sizeof(fresh.expected_sha256); ++i)
        fresh.expected_sha256[i] = expected_sha256[i];
    fresh.source = source;
    fresh.source_context = source_context;
    active_download = fresh;

    if (!open_and_begin()) {
        report(progress, progress_context);
        return false;
    }
    return transfer(progress, progress_context);
}

bool nexus_download_resume(nexus_download_progress_fn progress,
                           void *progress_context) {
    if (active_download.status.state != NEXUS_DOWNLOAD_PAUSED ||
        active_download.source_session == NULL ||
        !active_download.writer_open) return false;
    active_download.status.state = NEXUS_DOWNLOAD_ACTIVE;
    return transfer(progress, progress_context);
}

bool nexus_download_retry(nexus_download_progress_fn progress,
                          void *progress_context) {
    if (active_download.status.state != NEXUS_DOWNLOAD_FAILED) return false;
    active_download.status.error = NEXUS_DOWNLOAD_ERROR_NONE;
    active_download.status.bytes_received = 0U;
    active_download.status.percent = 0U;
    active_download.source_session = NULL;
    active_download.writer_open = false;
    if (!open_and_begin()) {
        report(progress, progress_context);
        return false;
    }
    return transfer(progress, progress_context);
}

bool nexus_download_cancel(void) {
    if (active_download.status.state != NEXUS_DOWNLOAD_ACTIVE &&
        active_download.status.state != NEXUS_DOWNLOAD_PAUSED) return false;
    source_close();
    if (active_download.writer_open) nxfs_write_file_abort();
    active_download.writer_open = false;
    active_download.status.state = NEXUS_DOWNLOAD_CANCELLED;
    active_download.status.error = NEXUS_DOWNLOAD_ERROR_CANCELLED;
    return true;
}

bool nexus_download_get_status(struct nexus_download_status *status) {
    if (status == NULL) return false;
    *status = active_download.status;
    return true;
}
