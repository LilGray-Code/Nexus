#ifndef NEXUS_SETTINGS_H
#define NEXUS_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_SETTINGS_RECORD_BYTES 16U
#define NEXUS_SETTINGS_ACCENT_COUNT 8U
#define NEXUS_SETTINGS_WALLPAPER_COUNT 4U

enum nexus_settings_theme {
    NEXUS_SETTINGS_THEME_DARK = 0,
    NEXUS_SETTINGS_THEME_LIGHT = 1
};

struct nexus_settings {
    uint8_t theme;
    uint8_t accent;
    uint8_t wallpaper;
    uint8_t ui_scale_percent;
    uint8_t cursor_size;
    uint8_t mouse_sensitivity_percent;
    uint8_t scroll_speed;
    uint8_t volume_percent;
    bool left_handed_mouse;
    bool high_contrast;
    bool audio_muted;
};

void nexus_settings_defaults(struct nexus_settings *settings);
bool nexus_settings_valid(const struct nexus_settings *settings);
bool nexus_settings_encode(const struct nexus_settings *settings,
                           uint8_t output[NEXUS_SETTINGS_RECORD_BYTES]);
bool nexus_settings_decode(const uint8_t *input, size_t length,
                           struct nexus_settings *settings);

#endif
