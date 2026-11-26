#ifndef NEXUS_DISPLAY_H
#define NEXUS_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

#define NEXUS_DISPLAY_MAX_MODES 32U
#define NEXUS_DISPLAY_MAX_EDID_TIMINGS 16U
#define NEXUS_DISPLAY_MAX_EDID_BYTES 512U

struct nexus_display_mode {
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bits_per_pixel;
    uint32_t refresh_hz;
    bool current_boot_mode;
    bool firmware_listed;
};

struct nexus_display_timing {
    uint32_t width;
    uint32_t height;
    uint32_t refresh_hz;
    uint32_t pixel_clock_khz;
    bool interlaced;
};

struct nexus_display_info {
    uint64_t current_width;
    uint64_t current_height;
    uint64_t current_pitch;
    uint16_t current_bits_per_pixel;
    uint8_t red_bits;
    uint8_t green_bits;
    uint8_t blue_bits;
    uint32_t current_refresh_hz;
    uint32_t firmware_mode_count;
    uint32_t mode_count;
    uint32_t timing_count;
    bool initialized;
    bool edid_valid;
    bool edid_extensions_complete;
    bool monitor_name_valid;
    char monitor_name[14];
    bool hdr_static_metadata;
    uint8_t hdr_eotf_flags;
    bool mode_set_available;
    bool last_rollback_succeeded;
    struct nexus_display_mode modes[NEXUS_DISPLAY_MAX_MODES];
    struct nexus_display_timing timings[NEXUS_DISPLAY_MAX_EDID_TIMINGS];
};

struct nexus_display_mode_backend {
    bool (*test_mode)(void *context,
                      const struct nexus_display_mode *mode);
    bool (*set_mode)(void *context,
                     const struct nexus_display_mode *mode);
    bool (*restore_mode)(void *context,
                         const struct nexus_display_mode *mode);
    bool (*supports_hdr)(void *context,
                         const struct nexus_display_mode *mode);
};

bool nexus_display_init(const struct limine_framebuffer *framebuffer);
const struct nexus_display_info *nexus_display_get_info(void);
const struct nexus_display_mode *nexus_display_mode_at(size_t index);

/* A real GPU driver must supply a test/apply/restore implementation. Merely
 * discovering EDID or firmware modes never enables mode changes.
 */
bool nexus_display_register_mode_backend(
    const struct nexus_display_mode_backend *backend, void *context);
bool nexus_display_apply_mode(size_t index);
bool nexus_display_hdr_monitor_supported(void);
bool nexus_display_hdr_output_available(void);

#endif
