#include "download.h"
#include "nxfs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

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

int main(void) {
    test_url_filename_and_query_removal();
    test_content_disposition_and_path_sanitization();
    test_mime_extension_and_fallback_name();
    test_collision_suffix_stays_before_extension();
    puts("NEXUS DOWNLOAD FILENAME TESTS PASSED");
    return 0;
}
