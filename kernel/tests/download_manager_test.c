#include "download_manager.h"
#include "nxfs.h"
#include "sha256.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_BYTES (5U * 1024U * 1024U + 17U)

static uint8_t stored[TEST_BYTES];
static uint32_t stored_size;
static uint32_t writer_size;
static uint32_t writer_used;
static bool writer_active;
static bool nxfs_writable = true;

bool nxfs_is_mounted(void) { return true; }
bool nxfs_is_writable(void) { return nxfs_writable; }
uint32_t nxfs_get_max_file_bytes(void) {
    return NEXUS_DOWNLOAD_MANAGER_MAX_BYTES;
}
bool nxfs_write_file_begin(const char *name, uint32_t expected_length) {
    if (name == NULL || strcmp(name, "LARGE.NUP") != 0 ||
        !nxfs_writable || writer_active || expected_length > sizeof(stored))
        return false;
    writer_active = true;
    writer_size = expected_length;
    writer_used = 0U;
    return true;
}
bool nxfs_write_file_append(const void *data, size_t length) {
    if (!writer_active || data == NULL ||
        length > writer_size - writer_used) return false;
    memcpy(stored + writer_used, data, length);
    writer_used += (uint32_t)length;
    return true;
}
bool nxfs_write_file_finish(void) {
    if (!writer_active || writer_used != writer_size) return false;
    writer_active = false;
    stored_size = writer_size;
    return true;
}
void nxfs_write_file_abort(void) {
    writer_active = false;
    writer_size = 0U;
    writer_used = 0U;
}
bool nxfs_is_writable(void);

struct fake_source {
    uint32_t offset;
    bool fail_once;
    bool failed;
};

static void fill_pattern(uint8_t *buffer, size_t length, uint32_t offset) {
    for (size_t i = 0U; i < length; ++i)
        buffer[i] = (uint8_t)(((offset + (uint32_t)i) * 29U) ^
                              ((offset + (uint32_t)i) >> 8));
}

static bool source_open(const char *url, void **session, void *context) {
    struct fake_source *source = (struct fake_source *)context;
    if (url == NULL || strcmp(url, "https://repo.example/large.nup") != 0 ||
        session == NULL || source == NULL) return false;
    source->offset = 0U;
    *session = source;
    return true;
}

static int source_read(void *session, uint8_t *buffer, size_t capacity,
                       void *context) {
    struct fake_source *source = (struct fake_source *)session;
    (void)context;
    if (source == NULL || buffer == NULL || capacity == 0U) return -1;
    if (source->fail_once && !source->failed && source->offset >= 8192U) {
        source->failed = true;
        return -1;
    }
    if (source->offset >= TEST_BYTES) return 0;
    size_t length = TEST_BYTES - source->offset;
    if (length > capacity) length = capacity;
    fill_pattern(buffer, length, source->offset);
    source->offset += (uint32_t)length;
    return (int)length;
}

static void source_close(void *session, void *context) {
    (void)session;
    (void)context;
}

static const struct nexus_download_source_ops source_ops = {
    .open = source_open,
    .read = source_read,
    .close = source_close
};

struct progress_fixture {
    bool pause_first;
    unsigned calls;
};

static enum nexus_download_control progress_callback(
    const struct nexus_download_status *status, void *context) {
    struct progress_fixture *fixture = (struct progress_fixture *)context;
    assert(status != NULL);
    ++fixture->calls;
    if (fixture->pause_first && fixture->calls == 1U)
        return NEXUS_DOWNLOAD_PAUSE;
    return NEXUS_DOWNLOAD_CONTINUE;
}

static void expected_digest(uint8_t digest[32]) {
    static uint8_t buffer[TEST_BYTES];
    fill_pattern(buffer, sizeof(buffer), 0U);
    nexus_sha256(buffer, sizeof(buffer), digest);
}

static void test_large_stream_pause_resume(void) {
    stored_size = 0U;
    struct fake_source source = {0};
    struct progress_fixture progress = {.pause_first = true};
    uint8_t digest[32];
    expected_digest(digest);
    assert(nexus_download_start(
        "https://repo.example/large.nup", "LARGE.NUP", TEST_BYTES, digest,
        &source_ops, &source, progress_callback, &progress));
    struct nexus_download_status status;
    assert(nexus_download_get_status(&status));
    assert(status.state == NEXUS_DOWNLOAD_PAUSED);
    assert(status.bytes_received == NEXUS_DOWNLOAD_MANAGER_CHUNK_BYTES);
    progress.pause_first = false;
    assert(nexus_download_resume(progress_callback, &progress));
    assert(nexus_download_get_status(&status));
    assert(status.state == NEXUS_DOWNLOAD_COMPLETE);
    assert(status.percent == 100U && status.bytes_received == TEST_BYTES);
    assert(stored_size == TEST_BYTES);
    for (uint32_t offset = 0U; offset < stored_size;) {
        uint8_t expected[512];
        const size_t amount = stored_size - offset > sizeof(expected)
            ? sizeof(expected) : stored_size - offset;
        fill_pattern(expected, amount, offset);
        assert(memcmp(stored + offset, expected, amount) == 0);
        offset += (uint32_t)amount;
    }
}

static void test_retry_restarts_after_transport_failure(void) {
    stored_size = 0U;
    struct fake_source source = {.fail_once = true};
    struct progress_fixture progress = {0};
    uint8_t digest[32];
    expected_digest(digest);
    assert(!nexus_download_start(
        "https://repo.example/large.nup", "LARGE.NUP", TEST_BYTES, digest,
        &source_ops, &source, progress_callback, &progress));
    struct nexus_download_status status;
    assert(nexus_download_get_status(&status));
    assert(status.state == NEXUS_DOWNLOAD_FAILED);
    assert(status.error == NEXUS_DOWNLOAD_ERROR_NETWORK);
    assert(nexus_download_retry(progress_callback, &progress));
    assert(nexus_download_get_status(&status));
    assert(status.state == NEXUS_DOWNLOAD_COMPLETE);
    assert(stored_size == TEST_BYTES);
}

static void test_integrity_failure_never_commits_file(void) {
    stored_size = 0U;
    struct fake_source source = {0};
    struct progress_fixture progress = {0};
    uint8_t wrong_digest[32] = {0};
    assert(!nexus_download_start(
        "https://repo.example/large.nup", "LARGE.NUP", TEST_BYTES,
        wrong_digest, &source_ops, &source,
        progress_callback, &progress));
    struct nexus_download_status status;
    assert(nexus_download_get_status(&status));
    assert(status.state == NEXUS_DOWNLOAD_FAILED);
    assert(status.error == NEXUS_DOWNLOAD_ERROR_INTEGRITY);
    assert(stored_size == 0U);
    assert(!writer_active);
}

static void test_cancel_discards_partial_file(void) {
    stored_size = 0U;
    struct fake_source source = {0};
    struct progress_fixture progress = {.pause_first = true};
    uint8_t digest[32];
    expected_digest(digest);
    assert(nexus_download_start(
        "https://repo.example/large.nup", "LARGE.NUP", TEST_BYTES, digest,
        &source_ops, &source, progress_callback, &progress));
    assert(nexus_download_cancel());
    struct nexus_download_status status;
    assert(nexus_download_get_status(&status));
    assert(status.state == NEXUS_DOWNLOAD_CANCELLED);
    assert(!writer_active && stored_size == 0U);
}

static void test_argument_and_storage_limits(void) {
    struct fake_source source = {0};
    uint8_t digest[32];
    expected_digest(digest);
    assert(!nexus_download_start(
        "http://repo.example/large.nup", "LARGE.NUP", TEST_BYTES, digest,
        &source_ops, &source, NULL, NULL));
    nxfs_writable = false;
    assert(!nexus_download_start(
        "https://repo.example/large.nup", "LARGE.NUP", TEST_BYTES, digest,
        &source_ops, &source, NULL, NULL));
    nxfs_writable = true;
    assert(!nexus_download_start(
        "https://repo.example/large.nup", "../LARGE.NUP", TEST_BYTES, digest,
        &source_ops, &source, NULL, NULL));
}

int main(void) {
    test_large_stream_pause_resume();
    test_retry_restarts_after_transport_failure();
    test_integrity_failure_never_commits_file();
    test_cancel_discards_partial_file();
    test_argument_and_storage_limits();
    puts("NEXUS STREAMING DOWNLOAD MANAGER TESTS PASSED");
    return 0;
}
