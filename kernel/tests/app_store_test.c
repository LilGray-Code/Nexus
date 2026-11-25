#include "app_store.h"
#include "vfs.h"
#include "boot_pack.h"
#include "process.h"

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
static bool mock_native_available[4];
static size_t mock_native_launches;
static char mock_native_last_launch[32];
static struct nexus_boot_pack_entry mock_native_entry;

const struct nexus_boot_pack_entry *nexus_boot_pack_find(
    const char *name) {
    static const char *const names[4] = {
        "KINETIC.ELF", "GAMEPAD.ELF", "AUDIO.ELF", "PIE.ELF"
    };
    if (name == NULL) return NULL;
    for (size_t i = 0U; i < 4U; ++i) {
        if (strcmp(name, names[i]) == 0 && mock_native_available[i]) {
            memset(&mock_native_entry, 0, sizeof(mock_native_entry));
            (void)snprintf(mock_native_entry.name,
                           sizeof(mock_native_entry.name), "%s", name);
            return &mock_native_entry;
        }
    }
    return NULL;
}
bool process_execute_root_file(const char *name,
                               struct nexus_process_result *result,
                               enum nexus_process_error *error) {
    if (name == NULL || result == NULL || nexus_boot_pack_find(name) == NULL) {
        if (error != NULL) *error = NEXUS_PROCESS_ERROR_NOT_FOUND;
        return false;
    }
    ++mock_native_launches;
    (void)snprintf(mock_native_last_launch, sizeof(mock_native_last_launch),
                   "%s", name);
    result->pid = 42U;
    result->exit_status = 0U;
    if (error != NULL) *error = NEXUS_PROCESS_ERROR_NONE;
    return true;
}

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
    mock_native_launches = 0U;
    mock_native_last_launch[0] = '\0';
    for (size_t i = 0U; i < 4U; ++i) mock_native_available[i] = false;
    for (size_t i = 0U; i < sizeof(mock_data); ++i) mock_data[i] = 0U;
}

static void test_catalog_has_web_and_native_entries(void) {
    assert(nexus_store_count() == NEXUS_STORE_MAX_APPS);
    const char *web_expected[] = {"STEAM", "DISCORD", "SPOTIFY",
                                  "GITHUB", "VSCODE", "ITCH"};
    for (size_t i = 0U; i < 6U; ++i) {
        const struct nexus_store_application *app =
            nexus_store_application_at(i);
        assert(app != NULL);
        assert(strcmp(app->id, web_expected[i]) == 0);
        assert(app->delivery == NEXUS_STORE_DELIVERY_WEB_SHORTCUT);
        assert(app->url != NULL && app->url[0] == 'h');
    }
    const char *native_expected[] = {
        "KINETIC.ELF", "GAMEPAD.ELF", "AUDIO.ELF", "PIE.ELF"
    };
    for (size_t i = 0U; i < 4U; ++i) {
        const size_t index = 6U + i;
        const struct nexus_store_application *app =
            nexus_store_application_at(index);
        assert(app != NULL);
        assert(app->delivery == NEXUS_STORE_DELIVERY_NATIVE_CLIENT);
        assert(strcmp(app->native_filename, native_expected[i]) == 0);
        assert(app->url == NULL);
        assert(!nexus_store_copy_url(index, mock_name, sizeof(mock_name)));
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

static void test_bundled_native_launch_and_read_only_lifecycle(void) {
    reset_mock();
    const size_t kinetic_index = 6U;
    assert(!nexus_store_is_installed(kinetic_index));
    assert(!nexus_store_launch_native(kinetic_index, NULL));

    mock_native_available[0] = true;
    assert(nexus_store_is_installed(kinetic_index));
    assert(!nexus_store_install_shortcut(kinetic_index));
    assert(!nexus_store_uninstall_shortcut(kinetic_index));
    uint64_t exit_status = UINT64_MAX;
    assert(nexus_store_launch_native(kinetic_index, &exit_status));
    assert(exit_status == 0U);
    assert(mock_native_launches == 1U);
    assert(strcmp(mock_native_last_launch, "KINETIC.ELF") == 0);

    mock_native_available[0] = false;
    assert(!nexus_store_is_installed(kinetic_index));
    assert(!nexus_store_launch_native(kinetic_index, &exit_status));
}

int main(void) {
    test_catalog_has_web_and_native_entries();
    test_install_open_and_remove_shortcut();
    test_collision_does_not_overwrite_user_data();
    test_bad_indices_and_failed_remove();
    test_bundled_native_launch_and_read_only_lifecycle();
    puts("NEXUS APP STORE NATIVE + WEB TESTS PASSED");
    return 0;
}
