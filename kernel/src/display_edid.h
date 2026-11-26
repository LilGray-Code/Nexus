#ifndef NEXUS_DISPLAY_EDID_H
#define NEXUS_DISPLAY_EDID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_EDID_BLOCK_SIZE 128U
#define NEXUS_EDID_MAX_EXTENSIONS 8U
#define NEXUS_EDID_MAX_BASE_MODES 4U

struct nexus_display_mode {
    uint32_t width;
    uint32_t height;
    uint32_t horizontal_total;
    uint32_t vertical_total;
    uint32_t pixel_clock_khz;
    uint32_t refresh_millihz;
    bool interlaced;
};

struct nexus_edid_info {
    uint16_t manufacturer_id;
    uint16_t product_code;
    uint32_t serial_number;
    uint8_t version;
    uint8_t revision;
    uint8_t extension_count;
    uint8_t mode_count;
    int8_t preferred_mode_index;
    struct nexus_display_mode modes[NEXUS_EDID_MAX_BASE_MODES];
};

/*
 * Validate the base EDID block and every declared extension checksum.
 * Extract the four base-block detailed timing descriptors. CTA/DisplayID
 * extension timings are validated but are not yet decoded.
 */
bool nexus_edid_parse(const uint8_t *bytes, size_t length,
                      struct nexus_edid_info *info);

#endif
