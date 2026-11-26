#ifndef NEXUS_DISPLAY_H
#define NEXUS_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

#define NEXUS_DISPLAY_MAX_MODES 32U
#define NEXUS_DISPLAY_MAX_EDID_TIMINGS 16U
#define NEXUS_DISPLAY_MAX_EDID_BYTES 1024U
#define NEXUS_DISPLAY_MAX_MONITORS 16U

enum nexus_display_rotation {
    NEXUS_DISPLAY_ROTATE_0 = LIMINE_FRAMEBUFFER_ROTATE_0,
    NEXUS_DISPLAY_ROTATE_90 = LIMINE_FRAMEBUFFER_ROTATE_90,
    NEXUS_DISPLAY_ROTATE_180 = LIMINE_FRAMEBUFFER_ROTATE_180,
    NEXUS_DISPLAY_ROTATE_270 = LIMINE_FRAMEBUFFER_ROTATE_270
};

enum nexus_display_topology {
    NEXUS_DISPLAY_TOPOLOGY_EXTENDED = 0,
    NEXUS_DISPLAY_TOPOLOGY_MIRRORED = 1
};

enum nexus_display_failure_stage {
    NEXUS_DISPLAY_FAILURE_NONE = 0,
    NEXUS_DISPLAY_FAILURE_TEST,
    NEXUS_DISPLAY_FAILURE_SET,
    NEXUS_DISPLAY_FAILURE_VERIFY,
    NEXUS_DISPLAY_FAILURE_ROLLBACK
};

struct nexus_display_mode {
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bits_per_pixel;
    uint32_t refresh_hz;
    bool current_boot_mode;
    bool firmware_listed;
    bool edid_timing;
};

struct nexus_display_timing {
    uint32_t width;
    uint32_t height;
    uint32_t refresh_hz;
    uint32_t pixel_clock_khz;
    bool interlaced;
};

struct nexus_display_monitor {
    uint32_t id;
    bool present;
    bool connected;
    bool enabled;
    bool primary;
    int32_t desktop_x;
    int32_t desktop_y;
    uint32_t width;
    uint32_t height;
    uint16_t dpi_x;
    uint16_t dpi_y;
    bool edid_valid;
    bool edid_extensions_complete;
    bool monitor_name_valid;
    char monitor_name[14];
    bool hdr_static_metadata;
    uint8_t hdr_eotf_flags;
    uint32_t timing_count;
    struct nexus_display_timing timings[NEXUS_DISPLAY_MAX_EDID_TIMINGS];
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
    bool last_mode_change_failed;
    enum nexus_display_failure_stage last_failure_stage;
    uint32_t upright_rotation;
    uint8_t scale_percent;
    uint8_t brightness_percent;
    uint16_t color_temperature_kelvin;
    bool software_brightness_available;
    bool software_night_light_available;
    uint32_t primary_monitor_id;
    uint32_t monitor_count;
    enum nexus_display_topology topology;
    struct nexus_display_monitor monitors[NEXUS_DISPLAY_MAX_MONITORS];
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
    bool (*verify_mode)(void *context,
                        const struct nexus_display_mode *mode);
    bool (*supports_hdr)(void *context,
                         const struct nexus_display_mode *mode);
    bool (*set_topology)(void *context,
                         enum nexus_display_topology topology);
    bool (*set_primary_monitor)(void *context, uint32_t monitor_id);
};

bool nexus_display_init(const struct limine_framebuffer *framebuffer);
const struct nexus_display_info *nexus_display_get_info(void);
const struct nexus_display_mode *nexus_display_mode_at(size_t index);
const struct nexus_display_monitor *nexus_display_monitor_at(size_t index);

/* Add one VirtIO/other-driver output. EDID is validated before it is retained. */
bool nexus_display_add_monitor(uint32_t id, bool connected,
                               bool enabled, int32_t desktop_x,
                               int32_t desktop_y,
                               uint32_t width, uint32_t height,
                               const uint8_t *edid, size_t edid_length);
bool nexus_display_set_primary_monitor(uint32_t id);
/* Extended/mirror layouts require a backend capable of programming all outputs. */
bool nexus_display_set_topology(enum nexus_display_topology topology);
bool nexus_display_topology_available(void);

/* Rotation is applied in the software framebuffer renderer. */
bool nexus_display_set_rotation(enum nexus_display_rotation rotation);
/* UI scaling is a logical desktop preference; it does not change scanout timing. */
bool nexus_display_set_scale_percent(uint8_t scale_percent);
/* These controls are software framebuffer effects, not panel backlight controls. */
bool nexus_display_set_brightness_percent(uint8_t brightness_percent);
bool nexus_display_set_color_temperature(uint16_t kelvin);

bool nexus_display_register_mode_backend(
    const struct nexus_display_mode_backend *backend, void *context);
/* Test, set, verify, and rollback to the last known-good mode on any failure. */
bool nexus_display_apply_mode(size_t index);
bool nexus_display_hdr_monitor_supported(void);
bool nexus_display_hdr_output_available(void);

#endif
