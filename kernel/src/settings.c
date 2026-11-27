#include "settings.h"

static const struct nexus_settings_category_info categories[
    NEXUS_SETTINGS_CATEGORY_COUNT] = {
    {NEXUS_SETTINGS_PERSONALIZATION, "Personalization",
     "theme accent wallpaper background colors desktop look"},
    {NEXUS_SETTINGS_DISPLAY, "Display",
     "monitor resolution refresh rate scaling dpi hdr rotation edid"},
    {NEXUS_SETTINGS_SOUND, "Sound",
     "audio volume mute speaker headphones microphone recording output"},
    {NEXUS_SETTINGS_NETWORK, "Network & Internet",
     "ethernet wifi wireless internet ip dhcp dns connection scan"},
    {NEXUS_SETTINGS_BLUETOOTH_DEVICES, "Bluetooth & Devices",
     "bluetooth pairing usb devices hardware peripherals"},
    {NEXUS_SETTINGS_INPUT_ACCESSORIES, "Input & Accessories",
     "keyboard mouse pointer scroll sensitivity gamepad controller"},
    {NEXUS_SETTINGS_ACCESSIBILITY, "Accessibility",
     "contrast text size pointer visibility accessibility"},
    {NEXUS_SETTINGS_APPLICATIONS, "Applications",
     "apps installed running processes permissions defaults startup"},
    {NEXUS_SETTINGS_STORAGE, "Storage",
     "disk drives partitions filesystem capacity formatting"},
    {NEXUS_SETTINGS_PRIVACY_SECURITY, "Privacy & Security",
     "privacy security sandbox permissions signatures trust"},
    {NEXUS_SETTINGS_ACCOUNTS_USERS, "Accounts & Users",
     "accounts users login password profiles sessions"},
    {NEXUS_SETTINGS_POWER_BATTERY, "Power & Battery",
     "power battery charging sleep suspend energy"},
    {NEXUS_SETTINGS_GAMING, "Gaming",
     "games performance fps frame pacing game library gpu"},
    {NEXUS_SETTINGS_UPDATES_RECOVERY, "Updates & Recovery",
     "updates upgrades recovery restore backup rollback"},
    {NEXUS_SETTINGS_SYSTEM_INFORMATION, "System Information",
     "system information cpu memory firmware bios acpi uptime hardware"},
    {NEXUS_SETTINGS_DEVELOPER, "Developer Settings",
     "developer debug diagnostics services console logging"}
};

static char ascii_lower(char value) {
    return value >= 'A' && value <= 'Z'
        ? (char)(value - 'A' + 'a') : value;
}

static bool contains_case_insensitive(const char *text, const char *query) {
    if (text == NULL || query == NULL) return false;
    for (size_t start = 0U; text[start] != '\0'; ++start) {
        size_t i = 0U;
        while (query[i] != '\0' && text[start + i] != '\0' &&
               ascii_lower(text[start + i]) == ascii_lower(query[i]))
            ++i;
        if (query[i] == '\0') return true;
    }
    return query[0] == '\0';
}

size_t nexus_settings_category_count(void) {
    return NEXUS_SETTINGS_CATEGORY_COUNT;
}

const struct nexus_settings_category_info *nexus_settings_category_at(
    size_t index) {
    return index < NEXUS_SETTINGS_CATEGORY_COUNT ? &categories[index] : NULL;
}

bool nexus_settings_category_matches(
    enum nexus_settings_category category, const char *query) {
    if (query == NULL) return false;
    for (size_t i = 0U; i < NEXUS_SETTINGS_CATEGORY_COUNT; ++i) {
        if (categories[i].category == category)
            return query[0] == '\0' ||
                   contains_case_insensitive(categories[i].label, query) ||
                   contains_case_insensitive(categories[i].search_terms, query);
    }
    return false;
}


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
        .high_contrast = false,
        .audio_muted = false
    };
}

bool nexus_settings_valid(const struct nexus_settings *settings) {
    static const uint8_t scales[] = {80U, 90U, 100U, 110U, 120U};
    static const uint8_t cursor_sizes[] = {1U, 2U, 3U};
    static const uint8_t mouse_speeds[] = {
        50U, 75U, 100U, 125U, 150U, 175U, 200U
    };
    static const uint8_t scroll_speeds[] = {1U, 2U, 3U, 4U, 5U};
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
        settings->volume_percent > 100U)
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
    output[14] = settings->audio_muted ? 1U : 0U;
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
        input[12] > 1U || input[13] > 1U || input[14] > 1U)
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
        .high_contrast = input[13] != 0U,
        .audio_muted = input[14] != 0U
    };
    if (!nexus_settings_valid(&decoded)) return false;
    *settings = decoded;
    return true;
}
