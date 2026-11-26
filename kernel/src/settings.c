#include "settings.h"

static bool value_in_set(uint8_t value, const uint8_t *set, size_t count) {
    for (size_t i = 0U; i < count; ++i)
        if (set[i] == value) return true;
    return false;
}

void nexus_settings_defaults(struct nexus_settings *settings) {
    if (settings == NULL) return;
    *settings = (struct nexus_settings){
        .theme = NEXUS_SETTINGS_THEME_DARK,
        .accent = 0U,
        .wallpaper = 0U,
        .ui_scale_percent = 100U,
        .cursor_size = 1U,
        .mouse_sensitivity_percent = 100U,
        .scroll_speed = 2U,
        .volume_percent = 75U,
        .left_handed_mouse = false,
        .high_contrast = false
    };
}

bool nexus_settings_valid(const struct nexus_settings *settings) {
    static const uint8_t scales[] = {80U, 90U, 100U, 110U, 120U};
    static const uint8_t cursor_sizes[] = {1U, 2U, 3U};
    static const uint8_t mouse_speeds[] = {
        50U, 75U, 100U, 125U, 150U, 175U, 200U
    };
    static const uint8_t scroll_speeds[] = {1U, 2U, 3U, 4U, 5U};
    static const uint8_t volumes[] = {0U, 25U, 50U, 75U, 100U};
    if (settings == NULL ||
        settings->theme > NEXUS_SETTINGS_THEME_LIGHT ||
        settings->accent >= NEXUS_SETTINGS_ACCENT_COUNT ||
        settings->wallpaper >= NEXUS_SETTINGS_WALLPAPER_COUNT ||
        !value_in_set(settings->ui_scale_percent, scales,
                      sizeof(scales) / sizeof(scales[0])) ||
        !value_in_set(settings->cursor_size, cursor_sizes,
                      sizeof(cursor_sizes) / sizeof(cursor_sizes[0])) ||
        !value_in_set(settings->mouse_sensitivity_percent, mouse_speeds,
                      sizeof(mouse_speeds) / sizeof(mouse_speeds[0])) ||
        !value_in_set(settings->scroll_speed, scroll_speeds,
                      sizeof(scroll_speeds) / sizeof(scroll_speeds[0])) ||
        !value_in_set(settings->volume_percent, volumes,
                      sizeof(volumes) / sizeof(volumes[0])))
        return false;
    return true;
}

bool nexus_settings_encode(const struct nexus_settings *settings,
                           uint8_t output[NEXUS_SETTINGS_RECORD_BYTES]) {
    if (output == NULL || !nexus_settings_valid(settings)) return false;
    output[0] = 'N';
    output[1] = 'X';
    output[2] = 'S';
    output[3] = '2';
    output[4] = settings->theme;
    output[5] = settings->accent;
    output[6] = settings->wallpaper;
    output[7] = settings->ui_scale_percent;
    output[8] = settings->cursor_size;
    output[9] = settings->mouse_sensitivity_percent;
    output[10] = settings->scroll_speed;
    output[11] = settings->volume_percent;
    output[12] = settings->left_handed_mouse ? 1U : 0U;
    output[13] = settings->high_contrast ? 1U : 0U;
    output[14] = 0U;
    uint8_t checksum = 0xA7U;
    for (size_t i = 0U; i < NEXUS_SETTINGS_RECORD_BYTES - 1U; ++i)
        checksum ^= output[i];
    output[15] = checksum;
    return true;
}

bool nexus_settings_decode(const uint8_t *input, size_t length,
                           struct nexus_settings *settings) {
    if (input == NULL || settings == NULL ||
        length != NEXUS_SETTINGS_RECORD_BYTES ||
        input[0] != 'N' || input[1] != 'X' ||
        input[2] != 'S' || input[3] != '2' ||
        input[12] > 1U || input[13] > 1U || input[14] != 0U)
        return false;
    uint8_t checksum = 0xA7U;
    for (size_t i = 0U; i < NEXUS_SETTINGS_RECORD_BYTES - 1U; ++i)
        checksum ^= input[i];
    if (checksum != input[15]) return false;

    const struct nexus_settings decoded = {
        .theme = input[4],
        .accent = input[5],
        .wallpaper = input[6],
        .ui_scale_percent = input[7],
        .cursor_size = input[8],
        .mouse_sensitivity_percent = input[9],
        .scroll_speed = input[10],
        .volume_percent = input[11],
        .left_handed_mouse = input[12] != 0U,
        .high_contrast = input[13] != 0U
    };
    if (!nexus_settings_valid(&decoded)) return false;
    *settings = decoded;
    return true;
}
