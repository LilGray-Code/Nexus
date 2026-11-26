#include "settings.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    struct nexus_settings settings;
    nexus_settings_defaults(&settings);
    assert(nexus_settings_valid(&settings));
    assert(settings.theme == NEXUS_SETTINGS_THEME_DARK);
    assert(settings.ui_scale_percent == 100U);
    assert(settings.cursor_size == 1U);
    assert(settings.volume_percent == 80U);

    uint8_t record[NEXUS_SETTINGS_RECORD_BYTES];
    assert(nexus_settings_encode(&settings, record));
    assert(record[0] == 'N' && record[1] == 'X' &&
           record[2] == 'S' && record[3] == '2');

    settings.theme = NEXUS_SETTINGS_THEME_LIGHT;
    settings.accent = 7U;
    settings.wallpaper = 3U;
    settings.ui_scale_percent = 120U;
    settings.cursor_size = 3U;
    settings.mouse_sensitivity_percent = 175U;
    settings.scroll_speed = 5U;
    settings.volume_percent = 25U;
    settings.left_handed_mouse = true;
    settings.high_contrast = true;
    assert(nexus_settings_encode(&settings, record));

    struct nexus_settings decoded = {0};
    assert(nexus_settings_decode(record, sizeof(record), &decoded));
    assert(decoded.theme == settings.theme);
    assert(decoded.accent == settings.accent);
    assert(decoded.wallpaper == settings.wallpaper);
    assert(decoded.ui_scale_percent == settings.ui_scale_percent);
    assert(decoded.cursor_size == settings.cursor_size);
    assert(decoded.mouse_sensitivity_percent ==
           settings.mouse_sensitivity_percent);
    assert(decoded.scroll_speed == settings.scroll_speed);
    assert(decoded.volume_percent == settings.volume_percent);
    assert(decoded.left_handed_mouse == settings.left_handed_mouse);
    assert(decoded.high_contrast == settings.high_contrast);

    uint8_t corrupted[NEXUS_SETTINGS_RECORD_BYTES];
    memcpy(corrupted, record, sizeof(corrupted));
    corrupted[9] ^= 1U;
    assert(!nexus_settings_decode(corrupted, sizeof(corrupted), &decoded));
    assert(!nexus_settings_decode(record, sizeof(record) - 1U, &decoded));

    corrupted[9] = 250U;
    /* Recompute checksum to prove validation rejects invalid values too. */
    uint8_t checksum = 0xA7U;
    for (size_t i = 0U; i < sizeof(corrupted) - 1U; ++i)
        checksum ^= corrupted[i];
    corrupted[15] = checksum;
    assert(!nexus_settings_decode(corrupted, sizeof(corrupted), &decoded));

    settings.ui_scale_percent = 105U;
    assert(!nexus_settings_encode(&settings, record));
    nexus_settings_defaults(NULL);
    return 0;
}
