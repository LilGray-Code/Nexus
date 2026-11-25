#include "app_store.h"

#include "vfs.h"
#include "boot_pack.h"
#include "process.h"

#include <stddef.h>
#include <stdint.h>

#define STORE_RECORD_PREFIX "NEXUS-WEB-SHORTCUT/1\nAPP="
#define STORE_RECORD_URL "\nURL="
#define STORE_RECORD_END "\n"

static const struct nexus_store_application applications[NEXUS_STORE_MAX_APPS] = {
    {
        "STEAM", "Steam", "Valve", "GAMES",
        "Browse the Steam catalog and community website.",
        "https://store.steampowered.com/",
        NEXUS_STORE_DELIVERY_WEB_SHORTCUT,
        "NEEDS WIN32 / PROTON RUNTIME", NULL
    },
    {
        "DISCORD", "Discord", "Discord", "COMMUNICATION",
        "Open the official Discord web application.",
        "https://discord.com/app",
        NEXUS_STORE_DELIVERY_WEB_SHORTCUT,
        "NEEDS FULL JS BROWSER", NULL
    },
    {
        "SPOTIFY", "Spotify", "Spotify", "MUSIC",
        "Open Spotify's official web player.",
        "https://open.spotify.com/",
        NEXUS_STORE_DELIVERY_WEB_SHORTCUT,
        "NEEDS DRM AUDIO SUPPORT", NULL
    },
    {
        "GITHUB", "GitHub", "GitHub", "DEVELOPMENT",
        "Browse repositories, releases and developer documentation.",
        "https://github.com/",
        NEXUS_STORE_DELIVERY_WEB_SHORTCUT,
        "NATIVE CLIENT NOT AVAILABLE", NULL
    },
    {
        "VSCODE", "VS Code for Web", "Microsoft", "DEVELOPMENT",
        "Open the official browser-based code editor.",
        "https://vscode.dev/",
        NEXUS_STORE_DELIVERY_WEB_SHORTCUT,
        "NEEDS JS / WASM SUPPORT", NULL
    },
    {
        "ITCH", "itch.io", "itch corp", "GAMES",
        "Discover independent games and creators.",
        "https://itch.io/",
        NEXUS_STORE_DELIVERY_WEB_SHORTCUT,
        "NEEDS GAME RUNTIME", NULL
    },
    {
        "KINETIC", "Kinetic Demo", "Nexus Project", "NATIVE DEMO",
        "Launch a bouncing-ball framebuffer demo. Press Escape to return.",
        NULL, NEXUS_STORE_DELIVERY_NATIVE_CLIENT,
        "BUNDLED NEXUS ELF - KEYBOARD / DISPLAY",
        "KINETIC.ELF"
    },
    {
        "GAMEPAD", "Gamepad Tester", "Nexus Project", "NATIVE DEMO",
        "Inspect connected USB HID controllers, axes and buttons. Press Escape to return.",
        NULL, NEXUS_STORE_DELIVERY_NATIVE_CLIENT,
        "BUNDLED NEXUS ELF - USB HID GAMEPAD",
        "GAMEPAD.ELF"
    },
    {
        "AUDIO", "Audio Test", "Nexus Project", "NATIVE DEMO",
        "Play a 440 Hz stereo tone when supported AC97 audio exists. Press Escape to stop.",
        NULL, NEXUS_STORE_DELIVERY_NATIVE_CLIENT,
        "BUNDLED NEXUS ELF - AC97 AUDIO OPTIONAL",
        "AUDIO.ELF"
    },
    {
        "PIE", "Native Runtime Check", "Nexus Project", "NATIVE DEMO",
        "Check the Nexus static-PIE loader and relative relocations.",
        NULL, NEXUS_STORE_DELIVERY_NATIVE_CLIENT,
        "BUNDLED NEXUS ELF - STATIC PIE",
        "PIE.ELF"
    },
    {
        "ARCADE", "Nexus Breakout", "Nexus Project", "GAMES",
        "A native keyboard and USB gamepad Breakout game. Press Escape to return.",
        NULL, NEXUS_STORE_DELIVERY_NATIVE_CLIENT,
        "BUNDLED NEXUS ELF - KEYBOARD / USB HID GAMEPAD",
        "ARCADE.ELF"
    }
};

static bool append_text(char *output, size_t capacity,
                        size_t *length, const char *text) {
    if (output == NULL || length == NULL || text == NULL || capacity == 0U)
        return false;
    for (size_t i = 0U; text[i] != '\0'; ++i) {
        if (*length + 1U >= capacity) return false;
        output[(*length)++] = text[i];
    }
    output[*length] = '\0';
    return true;
}

static bool make_record(const struct nexus_store_application *app,
                        char output[NEXUS_STORE_RECORD_MAX],
                        size_t *length) {
    if (app == NULL || output == NULL || length == NULL ||
        app->url == NULL || app->url[0] != 'h' ||
        app->url[1] != 't' || app->url[2] != 't' ||
        app->url[3] != 'p' || app->url[4] != 's' ||
        app->url[5] != ':' || app->url[6] != '/' ||
        app->url[7] != '/') return false;
    output[0] = '\0';
    *length = 0U;
    return append_text(output, NEXUS_STORE_RECORD_MAX, length,
                        STORE_RECORD_PREFIX) &&
           append_text(output, NEXUS_STORE_RECORD_MAX, length, app->id) &&
           append_text(output, NEXUS_STORE_RECORD_MAX, length,
                       STORE_RECORD_URL) &&
           append_text(output, NEXUS_STORE_RECORD_MAX, length, app->url) &&
           append_text(output, NEXUS_STORE_RECORD_MAX, length,
                       STORE_RECORD_END);
}

static bool shortcut_filename(const struct nexus_store_application *app,
                              char output[20]) {
    if (app == NULL || output == NULL || app->id == NULL) return false;
    static const char prefix[] = "APP-";
    static const char suffix[] = ".URL";
    size_t out = 0U;
    for (size_t i = 0U; i < sizeof(prefix) - 1U; ++i)
        output[out++] = prefix[i];
    for (size_t i = 0U; app->id[i] != '\0'; ++i) {
        const char c = app->id[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
        if (out + sizeof(suffix) >= 20U) return false;
        output[out++] = c;
    }
    if (out == sizeof(prefix) - 1U) return false;
    for (size_t i = 0U; i < sizeof(suffix); ++i)
        output[out++] = suffix[i];
    return true;
}

static bool read_record(const struct nexus_store_application *app,
                        char output[NEXUS_STORE_RECORD_MAX],
                        size_t *length) {
    if (app == NULL || output == NULL || length == NULL) return false;
    char filename[20];
    if (!shortcut_filename(app, filename)) return false;
    struct nexus_vfs_file file;
    if (!vfs_open_root_readonly(filename, &file)) return false;

    uint32_t file_size = 0U;
    bool valid = vfs_file_size(&file, &file_size) &&
                 file_size < NEXUS_STORE_RECORD_MAX;
    size_t received = 0U;
    if (valid) {
        valid = vfs_read(&file, output, file_size, &received) &&
                received == file_size;
    }
    vfs_close(&file);
    if (!valid) return false;
    output[received] = '\0';
    *length = received;
    return true;
}

size_t nexus_store_count(void) {
    return NEXUS_STORE_MAX_APPS;
}

const struct nexus_store_application *nexus_store_application_at(size_t index) {
    return index < NEXUS_STORE_MAX_APPS ? &applications[index] : NULL;
}

bool nexus_store_is_installed(size_t index) {
    const struct nexus_store_application *app =
        nexus_store_application_at(index);
    if (app == NULL) return false;
    if (app->delivery == NEXUS_STORE_DELIVERY_NATIVE_CLIENT)
        return app->native_filename != NULL &&
               nexus_boot_pack_find(app->native_filename) != NULL;
    char actual[NEXUS_STORE_RECORD_MAX];
    char expected[NEXUS_STORE_RECORD_MAX];
    size_t actual_length = 0U, expected_length = 0U;
    if (!read_record(app, actual, &actual_length) ||
        !make_record(app, expected, &expected_length) ||
        actual_length != expected_length) return false;
    for (size_t i = 0U; i < actual_length; ++i)
        if (actual[i] != expected[i]) return false;
    return true;
}

bool nexus_store_install_shortcut(size_t index) {
    const struct nexus_store_application *app =
        nexus_store_application_at(index);
    if (app == NULL ||
        app->delivery != NEXUS_STORE_DELIVERY_WEB_SHORTCUT) return false;
    if (nexus_store_is_installed(index)) return true;

    char filename[20];
    char record[NEXUS_STORE_RECORD_MAX];
    size_t length = 0U;
    if (!shortcut_filename(app, filename) ||
        !make_record(app, record, &length)) return false;

    /* Never overwrite an unrelated user file with a reserved store name. */
    struct nexus_vfs_file existing;
    if (vfs_open_root_readonly(filename, &existing)) {
        vfs_close(&existing);
        return false;
    }
    return vfs_write_root_file(filename, record, length);
}

bool nexus_store_uninstall_shortcut(size_t index) {
    const struct nexus_store_application *app =
        nexus_store_application_at(index);
    if (app == NULL ||
        app->delivery != NEXUS_STORE_DELIVERY_WEB_SHORTCUT ||
        !nexus_store_is_installed(index)) return false;
    char filename[20];
    return shortcut_filename(app, filename) &&
           vfs_remove_root_file(filename);
}

bool nexus_store_copy_url(size_t index, char *output, size_t capacity) {
    const struct nexus_store_application *app =
        nexus_store_application_at(index);
    if (app == NULL || output == NULL || capacity == 0U ||
        app->delivery != NEXUS_STORE_DELIVERY_WEB_SHORTCUT ||
        app->url == NULL) return false;
    size_t length = 0U;
    while (app->url[length] != '\0') {
        if (length + 1U >= capacity) return false;
        output[length] = app->url[length];
        ++length;
    }
    output[length] = '\0';
    return true;
}


bool nexus_store_launch_native(size_t index, uint64_t *exit_status) {
    const struct nexus_store_application *app =
        nexus_store_application_at(index);
    if (exit_status != NULL) *exit_status = UINT64_MAX;
    if (app == NULL ||
        app->delivery != NEXUS_STORE_DELIVERY_NATIVE_CLIENT ||
        app->native_filename == NULL ||
        nexus_boot_pack_find(app->native_filename) == NULL) return false;

    struct nexus_process_result result = {0};
    enum nexus_process_error error = NEXUS_PROCESS_ERROR_NONE;
    if (!process_execute_root_file(app->native_filename, &result, &error))
        return false;
    if (exit_status != NULL) *exit_status = result.exit_status;
    return true;
}
