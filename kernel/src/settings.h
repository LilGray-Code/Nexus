#ifndef NEXUS_SETTINGS_H
#define NEXUS_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_SETTINGS_RECORD_BYTES 16U
#define NEXUS_SETTINGS_ACCENT_COUNT 8U
#define NEXUS_SETTINGS_WALLPAPER_COUNT 4U
#define NEXUS_SETTINGS_CATEGORY_COUNT 16U
#define NEXUS_SETTINGS_SEARCH_MAX 31U

enum nexus_settings_category {
    NEXUS_SETTINGS_PERSONALIZATION = 0,
    NEXUS_SETTINGS_DISPLAY,
    NEXUS_SETTINGS_SOUND,
    NEXUS_SETTINGS_NETWORK,
    NEXUS_SETTINGS_BLUETOOTH_DEVICES,
    NEXUS_SETTINGS_INPUT_ACCESSORIES,
    NEXUS_SETTINGS_ACCESSIBILITY,
    NEXUS_SETTINGS_APPLICATIONS,
    NEXUS_SETTINGS_STORAGE,
    NEXUS_SETTINGS_PRIVACY_SECURITY,
    NEXUS_SETTINGS_ACCOUNTS_USERS,
    NEXUS_SETTINGS_POWER_BATTERY,
    NEXUS_SETTINGS_GAMING,
    NEXUS_SETTINGS_UPDATES_RECOVERY,
    NEXUS_SETTINGS_SYSTEM_INFORMATION,
    NEXUS_SETTINGS_DEVELOPER,
    NEXUS_SETTINGS_CATEGORY_INVALID = 0xFF
};

struct nexus_settings_category_info {
    enum nexus_settings_category category;
    const char *label;
    const char *search_terms;
};

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

size_t nexus_settings_category_count(void);
const struct nexus_settings_category_info *nexus_settings_category_at(
    size_t index);
bool nexus_settings_category_matches(
    enum nexus_settings_category category, const char *query);

void nexus_settings_defaults(struct nexus_settings *settings);
bool nexus_settings_valid(const struct nexus_settings *settings);
bool nexus_settings_encode(const struct nexus_settings *settings,
                           uint8_t output[NEXUS_SETTINGS_RECORD_BYTES]);
bool nexus_settings_decode(const uint8_t *input, size_t length,
                           struct nexus_settings *settings);

#endif
