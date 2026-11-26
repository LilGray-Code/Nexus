#include "download.h"
#include "nxfs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t sha256_abc[NEXUS_SHA256_SIZE] = {
    0xBAU,0x78U,0x16U,0xBFU,0x8FU,0x01U,0xCFU,0xEAU,
    0x41U,0x41U,0x40U,0xDEU,0x5DU,0xAEU,0x22U,0x3BU,
    0x00U,0x36U,0x1AU,0x39U,0x61U,0x77U,0xA9U,0xCBU,
    0x41U,0x0FU,0xF6U,0x1FU,0x20U,0x01U,0x5AU,0xADU
};

static void test_url_filename_and_query_removal(void) {
    char name[NEXUS_DOWNLOAD_NAME_MAX];
    assert(nexus_download_filename(
        "https://cdn.example.test/releases/my%20photo.PNG?token=abc",
        NULL, "image/png; charset=binary", name));
    assert(strcmp(name, "MY_PHOTO.PNG") == 0);
}

static void test_content_disposition_and_path_sanitization(void) {
    char name[NEXUS_DOWNLOAD_NAME_MAX];
    assert(nexus_download_filename(
        "https://example.test/download?id=7",
        "attachment; filename=\"../../Nexus Setup.exe\"",
        "application/octet-stream", name));
    assert(strcmp(name, "NEXUS_SETUP.EXE") == 0);
}

static void test_mime_extension_and_fallback_name(void) {
    char name[NEXUS_DOWNLOAD_NAME_MAX];
    assert(nexus_download_filename(
        "https://example.test/get?id=7", NULL, "application/pdf", name));
    assert(strcmp(name, "GET.PDF") == 0);
    assert(nexus_download_filename(
        "https://example.test/readme", NULL, "text/plain; charset=utf-8", name));
    assert(strcmp(name, "README.TXT") == 0);
}

static void test_collision_suffix_stays_before_extension(void) {
    char name[NEXUS_DOWNLOAD_NAME_MAX];
    assert(nexus_download_unique_name("APP.ZIP", 0U, name));
    assert(strcmp(name, "APP.ZIP") == 0);
    assert(nexus_download_unique_name("APP.ZIP", 12U, name));
    assert(strcmp(name, "APP_12.ZIP") == 0);
}

_Static_assert(NEXUS_NXFS_MAX_FILE_BYTES == (256U * 1024U * 1024U),
               "new NXFS volumes support per-file slots up to 256 MiB");

_Static_assert(NEXUS_DOWNLOAD_MAX_BYTES == (256U * 1024U * 1024U),
               "browser streaming download limit must be 256 MiB");


static void test_streaming_job_progress_and_sha256(void) {
    struct nexus_download_job job;
    assert(nexus_download_job_begin(&job, 1024U, true, 3U, sha256_abc));
    assert(nexus_download_job_progress_percent(&job) == 0U);
    assert(nexus_download_job_append(&job, "a", 1U));
    assert(nexus_download_job_progress_percent(&job) == 33U);
    assert(nexus_download_job_append(&job, "bc", 2U));
    assert(nexus_download_job_progress_percent(&job) == 100U);
    assert(nexus_download_job_finish(&job));
    assert(job.state == NEXUS_DOWNLOAD_JOB_COMPLETE);
    assert(job.sha256_valid && job.checksum_verified);
    char hex[65];
    assert(nexus_download_sha256_hex(job.actual_sha256, hex));
    assert(strcmp(hex,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
        == 0);
}

static void test_streaming_job_rejects_bad_length_and_checksum(void) {
    struct nexus_download_job job;
    assert(nexus_download_job_begin(&job, 64U, true, 4U, sha256_abc));
    assert(nexus_download_job_append(&job, "abc", 3U));
    assert(!nexus_download_job_finish(&job));
    assert(job.state == NEXUS_DOWNLOAD_JOB_FAILED);

    assert(nexus_download_job_begin(&job, 64U, true, 3U, sha256_abc));
    assert(nexus_download_job_append(&job, "abd", 3U));
    assert(!nexus_download_job_finish(&job));
    assert(job.state == NEXUS_DOWNLOAD_JOB_FAILED);
    assert(job.sha256_valid && !job.checksum_verified);
}

static void test_streaming_job_unknown_length_limit_and_cancel(void) {
    struct nexus_download_job job;
    assert(nexus_download_job_begin(&job, 4U, false, 0U, NULL));
    assert(nexus_download_job_progress_percent(&job) == UINT32_MAX);
    assert(nexus_download_job_append(&job, "1234", 4U));
    assert(!nexus_download_job_append(&job, "5", 1U));
    assert(job.state == NEXUS_DOWNLOAD_JOB_FAILED);

    assert(nexus_download_job_begin(&job, 1024U, false, 0U, NULL));
    assert(nexus_download_job_append(&job, "partial", 7U));
    nexus_download_job_cancel(&job);
    assert(job.state == NEXUS_DOWNLOAD_JOB_CANCELLED);
    assert(!nexus_download_job_finish(&job));
}

int main(void) {
    test_url_filename_and_query_removal();
    test_content_disposition_and_path_sanitization();
    test_mime_extension_and_fallback_name();
    test_collision_suffix_stays_before_extension();
    test_streaming_job_progress_and_sha256();
    test_streaming_job_rejects_bad_length_and_checksum();
    test_streaming_job_unknown_length_limit_and_cancel();
    puts("NEXUS DOWNLOAD FILENAME TESTS PASSED");
    return 0;
}
