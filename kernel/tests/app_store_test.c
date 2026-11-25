#include "app_store.h"
#include "vfs.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MOCK_CAPACITY 256U
static char mock_name[20];
static uint8_t mock_data[MOCK_CAPACITY];
static size_t mock_size;
static bool mock_present;
static bool mock_remove_allowed = true;

bool vfs_open_root_readonly(const char *name, struct nexus_vfs_file *file) {
    if (name == NULL || file == NULL || !mock_present ||
        strcmp(name, mock_name) != 0) return false;
    *file = (struct nexus_vfs_file){0};
    file->filesystem_entry = mock_data;
    file->open = true;
    file->ram_file = true;
    return true;
}
bool vfs_file_size(const struct nexus_vfs_file *file, uint32_t *size) {
    if (file == NULL || !file->open || size == NULL) return false;
    *size = (uint32_t)mock_size;
    return true;
}
bool vfs_read(struct nexus_vfs_file *file, void *buffer,
              size_t requested, size_t *bytes_read) {
    if (file == NULL || !file->open || bytes_read == NULL ||
        (requested != 0U && buffer == NULL) ||
        requested > mock_size - file->offset) return false;
    for (size_t i = 0U; i < requested; ++i)
        ((uint8_t *)buffer)[i] = mock_data[file->offset + i];
    file->offset += (uint32_t)requested;
    *bytes_read = requested;
    return true;
}
void vfs_close(struct nexus_vfs_file *file) {
    if (file != NULL) *file = (struct nexus_vfs_file){0};
}
bool vfs_write_root_file(const char *name, const void *data, size_t length) {
    if (name == NULL || data == NULL || length > sizeof(mock_data))
        return false;
    if (mock_present) return false;
    size_t i = 0U;
    while (name[i] != '\0' && i + 1U < sizeof(mock_name)) {
        mock_name[i] = name[i];
        ++i;
    }
    if (name[i] != '\0') return false;
    mock_name[i] = '\0';
    for (size_t n = 0U; n < length; ++n)
        mock_data[n] = ((const uint8_t *)data)[n];
    mock_size = length;
    mock_present = true;
    return true;
}
bool vfs_remove_root_file(const char *name) {
    if (!mock_remove_allowed || !mock_present ||
        strcmp(name, mock_name) != 0) return false;
    mock_present = false;
    mock_size = 0U;
    mock_name[0] = '\0';
    return true;
}

static bool bytes_contain(const uint8_t *data, size_t data_length,
                          const char *needle) {
    size_t needle_length = strlen(needle);
    if (needle_length == 0U || needle_length > data_length) return false;
    for (size_t start = 0U; start + needle_length <= data_length; ++start) {
        size_t i = 0U;
        while (i < needle_length &&
               data[start + i] == (uint8_t)needle[i]) ++i;
        if (i == needle_length) return true;
    }
    return false;
}

static void reset_mock(void) {
    mock_name[0] = '\0';
    mock_size = 0U;
    mock_present = false;
    mock_remove_allowed = true;
    for (size_t i = 0U; i < sizeof(mock_data); ++i) mock_data[i] = 0U;
}

static void test_catalog_has_official_https_entries(void) {
    assert(nexus_store_count() == NEXUS_STORE_MAX_APPS);
    const char *expected[] = {"STEAM", "DISCORD", "SPOTIFY",
                              "GITHUB", "VSCODE", "ITCH"};
    for (size_t i = 0U; i < nexus_store_count(); ++i) {
        const struct nexus_store_application *app =
            nexus_store_application_at(i);
        assert(app != NULL);
        assert(strcmp(app->id, expected[i]) == 0);
        assert(app->url[0] == 'h' && app->url[1] == 't' &&
               app->url[2] == 't' && app->url[3] == 'p' &&
               app->url[4] == 's' && app->url[5] == ':');
    }
    assert(nexus_store_application_at(NEXUS_STORE_MAX_APPS) == NULL);
}

static void test_install_open_and_remove_shortcut(void) {
    reset_mock();
    assert(!nexus_store_is_installed(0U));
    assert(nexus_store_install_shortcut(0U));
    assert(nexus_store_is_installed(0U));
    assert(strcmp(mock_name, "APP-STEAM.URL") == 0);
    assert(bytes_contain(mock_data, mock_size,
                         "https://store.steampowered.com/"));
    assert(nexus_store_install_shortcut(0U)); /* Idempotent */
    assert(nexus_store_uninstall_shortcut(0U));
    assert(!nexus_store_is_installed(0U));
}

static void test_collision_does_not_overwrite_user_data(void) {
    reset_mock();
    static const char user_data[] = "my notes";
    assert(vfs_write_root_file("APP-DISCORD.URL",
                               user_data, sizeof(user_data) - 1U));
    assert(!nexus_store_install_shortcut(1U));
    assert(mock_size == sizeof(user_data) - 1U);
    assert(memcmp(mock_data, user_data, mock_size) == 0);
    assert(!nexus_store_uninstall_shortcut(1U));
}

static void test_bad_indices_and_failed_remove(void) {
    reset_mock();
    assert(!nexus_store_install_shortcut(NEXUS_STORE_MAX_APPS));
    assert(!nexus_store_uninstall_shortcut(NEXUS_STORE_MAX_APPS));
    assert(!nexus_store_copy_url(NEXUS_STORE_MAX_APPS, mock_name,
                                 sizeof(mock_name)));
    assert(nexus_store_install_shortcut(2U));
    mock_remove_allowed = false;
    assert(!nexus_store_uninstall_shortcut(2U));
    assert(nexus_store_is_installed(2U));
}

int main(void) {
    test_catalog_has_official_https_entries();
    test_install_open_and_remove_shortcut();
    test_collision_does_not_overwrite_user_data();
    test_bad_indices_and_failed_remove();
    puts("NEXUS APP STORE SHORTCUT TESTS PASSED");
    return 0;
}
