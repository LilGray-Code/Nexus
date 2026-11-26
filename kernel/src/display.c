#include "display.h"

#define EDID_BLOCK_BYTES 128U
#define EDID_BASE_BYTES 128U
#define EDID_CTA_TAG 0x02U
#define EDID_CTA_HDR_STATIC_METADATA 0x06U

static struct nexus_display_info display_info;
static struct nexus_display_mode_backend display_backend;
static void *display_backend_context;
static bool display_backend_registered;

static bool edid_header_valid(const uint8_t *edid) {
    static const uint8_t header[8] = {
        0x00U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x00U
    };
    if (edid == NULL) return false;
    for (size_t i = 0U; i < sizeof(header); ++i)
        if (edid[i] != header[i]) return false;
    return true;
}

static bool edid_checksum_valid(const uint8_t *block) {
    uint8_t sum = 0U;
    if (block == NULL) return false;
    for (size_t i = 0U; i < EDID_BLOCK_BYTES; ++i)
        sum = (uint8_t)(sum + block[i]);
    return sum == 0U;
}

static bool timing_equal(const struct nexus_display_timing *a,
                         const struct nexus_display_timing *b) {
    return a->width == b->width && a->height == b->height &&
           a->refresh_hz == b->refresh_hz &&
           a->interlaced == b->interlaced;
}

static void add_timing(const struct nexus_display_timing *timing) {
    if (timing == NULL || timing->width == 0U || timing->height == 0U ||
        timing->refresh_hz == 0U ||
        timing->width > 16384U || timing->height > 16384U) return;

    for (uint32_t i = 0U; i < display_info.timing_count; ++i)
        if (timing_equal(&display_info.timings[i], timing)) return;

    if (display_info.timing_count >= NEXUS_DISPLAY_MAX_EDID_TIMINGS) return;
    display_info.timings[display_info.timing_count++] = *timing;
}

static void parse_detailed_timing(const uint8_t *descriptor) {
    if (descriptor == NULL) return;
    const uint32_t pixel_clock_10khz =
        (uint32_t)descriptor[0] | ((uint32_t)descriptor[1] << 8);
    if (pixel_clock_10khz == 0U) return;

    const uint32_t horizontal_active =
        (uint32_t)descriptor[2] |
        ((uint32_t)(descriptor[4] & 0xF0U) << 4);
    const uint32_t horizontal_blank =
        (uint32_t)descriptor[3] |
        ((uint32_t)(descriptor[4] & 0x0FU) << 8);
    const uint32_t vertical_active =
        (uint32_t)descriptor[5] |
        ((uint32_t)(descriptor[7] & 0xF0U) << 4);
    const uint32_t vertical_blank =
        (uint32_t)descriptor[6] |
        ((uint32_t)(descriptor[7] & 0x0FU) << 8);
    const uint32_t horizontal_total =
        horizontal_active + horizontal_blank;
    const uint32_t vertical_total = vertical_active + vertical_blank;
    if (horizontal_total == 0U || vertical_total == 0U ||
        horizontal_active == 0U || vertical_active == 0U) return;

    const uint64_t denominator =
        (uint64_t)horizontal_total * (uint64_t)vertical_total;
    const uint64_t pixel_clock_hz = (uint64_t)pixel_clock_10khz * 10000U;
    uint64_t refresh = (pixel_clock_hz + denominator / 2U) / denominator;
    const bool interlaced = (descriptor[17] & 0x80U) != 0U;
    if (interlaced) refresh *= 2U;
    if (refresh > UINT32_MAX) return;

    const struct nexus_display_timing timing = {
        .width = horizontal_active,
        .height = vertical_active,
        .refresh_hz = (uint32_t)refresh,
        .pixel_clock_khz = pixel_clock_10khz * 10U,
        .interlaced = interlaced
    };
    add_timing(&timing);
}

static void parse_monitor_name(const uint8_t *descriptor) {
    if (descriptor == NULL || display_info.monitor_name_valid ||
        descriptor[0] != 0U || descriptor[1] != 0U ||
        descriptor[2] != 0U || descriptor[3] != 0xFCU) return;

    size_t length = 0U;
    for (size_t i = 5U; i < 18U && length < sizeof(display_info.monitor_name) - 1U; ++i) {
        const uint8_t character = descriptor[i];
        if (character == 0U || character == '\n' || character == '\r') break;
        if (character < 0x20U || character > 0x7EU) continue;
        display_info.monitor_name[length++] = (char)character;
    }
    while (length != 0U && display_info.monitor_name[length - 1U] == ' ')
        --length;
    display_info.monitor_name[length] = '\0';
    display_info.monitor_name_valid = length != 0U;
}

static void parse_cta_extension(const uint8_t *block) {
    if (block == NULL || block[0] != EDID_CTA_TAG) return;

    uint32_t data_end = block[2];
    if (data_end == 0U) data_end = 127U;
    if (data_end < 4U || data_end > 127U) return;

    uint32_t cursor = 4U;
    while (cursor < data_end) {
        const uint8_t header = block[cursor++];
        const uint8_t tag = (uint8_t)(header >> 5);
        const uint8_t length = (uint8_t)(header & 0x1FU);
        if ((uint32_t)length > data_end - cursor) break;

        if (tag == 7U && length >= 3U &&
            block[cursor] == EDID_CTA_HDR_STATIC_METADATA) {
            const uint8_t eotf = block[cursor + 1U];
            const uint8_t metadata = block[cursor + 2U];
            display_info.hdr_eotf_flags |= eotf;
            if ((eotf & 0x0CU) != 0U && metadata != 0U)
                display_info.hdr_static_metadata = true;
        }
        cursor += length;
    }

    /* CTA detailed timings begin at the DTD offset and end before checksum. */
    for (uint32_t offset = data_end; offset + 18U <= 127U; offset += 18U)
        parse_detailed_timing(block + offset);
}

static bool parse_edid(const uint8_t *edid, size_t length) {
    if (edid == NULL || length < EDID_BASE_BYTES ||
        length > NEXUS_DISPLAY_MAX_EDID_BYTES ||
        (length % EDID_BLOCK_BYTES) != 0U ||
        !edid_header_valid(edid) || !edid_checksum_valid(edid))
        return false;

    for (uint32_t offset = 54U; offset + 18U <= 126U; offset += 18U) {
        const uint8_t *descriptor = edid + offset;
        if (descriptor[0] == 0U && descriptor[1] == 0U &&
            descriptor[2] == 0U) {
            parse_monitor_name(descriptor);
        } else {
            parse_detailed_timing(descriptor);
        }
    }

    const size_t available_blocks = length / EDID_BLOCK_BYTES;
    size_t declared_blocks = (size_t)edid[126] + 1U;
    if (declared_blocks > available_blocks) {
        declared_blocks = available_blocks;
        display_info.edid_extensions_complete = false;
    } else {
        display_info.edid_extensions_complete = true;
    }

    for (size_t block = 1U; block < declared_blocks; ++block) {
        const uint8_t *extension = edid + block * EDID_BLOCK_BYTES;
        if (!edid_checksum_valid(extension)) continue;
        if (extension[0] == EDID_CTA_TAG)
            parse_cta_extension(extension);
    }
    return true;
}

static void add_firmware_mode(const struct limine_video_mode *mode) {
    if (mode == NULL || mode->width == 0U || mode->height == 0U ||
        mode->width > 16384U || mode->height > 16384U ||
        (mode->bpp != 16U && mode->bpp != 24U && mode->bpp != 32U))
        return;
    const uint64_t bytes_per_pixel = ((uint64_t)mode->bpp + 7U) / 8U;
    if (mode->width > UINT64_MAX / bytes_per_pixel ||
        mode->pitch < mode->width * bytes_per_pixel) return;

    for (uint32_t i = 0U; i < display_info.mode_count; ++i) {
        struct nexus_display_mode *saved = &display_info.modes[i];
        if (saved->width == mode->width &&
            saved->height == mode->height &&
            saved->bits_per_pixel == mode->bpp) {
            saved->firmware_listed = true;
            saved->pitch = mode->pitch;
            ++display_info.firmware_mode_count;
            return;
        }
    }
    if (display_info.mode_count >= NEXUS_DISPLAY_MAX_MODES) return;

    display_info.modes[display_info.mode_count++] =
        (struct nexus_display_mode){
            .width = mode->width,
            .height = mode->height,
            .pitch = mode->pitch,
            .bits_per_pixel = mode->bpp,
            .refresh_hz = 0U,
            .current_boot_mode = false,
            .firmware_listed = true
        };
    ++display_info.firmware_mode_count;
}

static uint32_t timing_refresh_for_mode(uint64_t width, uint64_t height) {
    for (uint32_t i = 0U; i < display_info.timing_count; ++i) {
        const struct nexus_display_timing *timing = &display_info.timings[i];
        if (timing->width == width && timing->height == height)
            return timing->refresh_hz;
    }
    return 0U;
}

static void add_current_mode(void) {
    for (uint32_t i = 0U; i < display_info.mode_count; ++i) {
        struct nexus_display_mode *mode = &display_info.modes[i];
        if (mode->width == display_info.current_width &&
            mode->height == display_info.current_height &&
            mode->bits_per_pixel == display_info.current_bits_per_pixel) {
            mode->current_boot_mode = true;
            if (mode->refresh_hz == 0U)
                mode->refresh_hz = timing_refresh_for_mode(mode->width, mode->height);
            display_info.current_refresh_hz = mode->refresh_hz;
            return;
        }
    }

    if (display_info.mode_count >= NEXUS_DISPLAY_MAX_MODES) return;
    const uint32_t refresh = timing_refresh_for_mode(
        display_info.current_width, display_info.current_height);
    display_info.modes[display_info.mode_count++] =
        (struct nexus_display_mode){
            .width = display_info.current_width,
            .height = display_info.current_height,
            .pitch = display_info.current_pitch,
            .bits_per_pixel = display_info.current_bits_per_pixel,
            .refresh_hz = refresh,
            .current_boot_mode = true,
            .firmware_listed = false
        };
    display_info.current_refresh_hz = refresh;
}

bool nexus_display_init(const struct limine_framebuffer *framebuffer) {
    display_info = (struct nexus_display_info){0};
    display_backend = (struct nexus_display_mode_backend){0};
    display_backend_context = NULL;
    display_backend_registered = false;

    if (framebuffer == NULL || framebuffer->address == NULL ||
        framebuffer->width == 0U || framebuffer->height == 0U ||
        framebuffer->width > 16384U || framebuffer->height > 16384U ||
        framebuffer->pitch == 0U || framebuffer->bpp == 0U)
        return false;

    display_info.current_width = framebuffer->width;
    display_info.current_height = framebuffer->height;
    display_info.current_pitch = framebuffer->pitch;
    display_info.current_bits_per_pixel = framebuffer->bpp;
    display_info.red_bits = framebuffer->red_mask_size;
    display_info.green_bits = framebuffer->green_mask_size;
    display_info.blue_bits = framebuffer->blue_mask_size;
    display_info.last_rollback_succeeded = true;

    if (framebuffer->edid != NULL &&
        framebuffer->edid_size >= EDID_BASE_BYTES &&
        framebuffer->edid_size <= NEXUS_DISPLAY_MAX_EDID_BYTES) {
        display_info.edid_valid = parse_edid(
            (const uint8_t *)framebuffer->edid,
            (size_t)framebuffer->edid_size);
    }

    if (framebuffer->modes != NULL) {
        uint64_t count = framebuffer->mode_count;
        if (count > NEXUS_DISPLAY_MAX_MODES) count = NEXUS_DISPLAY_MAX_MODES;
        for (uint64_t i = 0U; i < count; ++i)
            add_firmware_mode(framebuffer->modes[i]);
    }
    add_current_mode();
    display_info.initialized = true;
    return true;
}

const struct nexus_display_info *nexus_display_get_info(void) {
    return display_info.initialized ? &display_info : NULL;
}

const struct nexus_display_mode *nexus_display_mode_at(size_t index) {
    if (!display_info.initialized || index >= display_info.mode_count)
        return NULL;
    return &display_info.modes[index];
}

bool nexus_display_register_mode_backend(
    const struct nexus_display_mode_backend *backend, void *context) {
    if (backend == NULL || backend->test_mode == NULL ||
        backend->set_mode == NULL || backend->restore_mode == NULL) {
        display_backend = (struct nexus_display_mode_backend){0};
        display_backend_context = NULL;
        display_backend_registered = false;
        display_info.mode_set_available = false;
        return false;
    }
    display_backend = *backend;
    display_backend_context = context;
    display_backend_registered = true;
    display_info.mode_set_available = true;
    return true;
}

bool nexus_display_apply_mode(size_t index) {
    if (!display_info.initialized || !display_backend_registered ||
        index >= display_info.mode_count) return false;
    const struct nexus_display_mode previous = {
        .width = display_info.current_width,
        .height = display_info.current_height,
        .pitch = display_info.current_pitch,
        .bits_per_pixel = display_info.current_bits_per_pixel,
        .refresh_hz = display_info.current_refresh_hz,
        .current_boot_mode = true,
        .firmware_listed = true
    };
    const struct nexus_display_mode target = display_info.modes[index];
    if (target.width == previous.width && target.height == previous.height &&
        target.bits_per_pixel == previous.bits_per_pixel &&
        target.refresh_hz == previous.refresh_hz)
        return true;

    if (!display_backend.test_mode(display_backend_context, &target))
        return false;

    if (!display_backend.set_mode(display_backend_context, &target)) {
        display_info.last_rollback_succeeded =
            display_backend.restore_mode(display_backend_context, &previous);
        if (!display_info.last_rollback_succeeded)
            display_info.mode_set_available = false;
        return false;
    }

    display_info.current_width = target.width;
    display_info.current_height = target.height;
    display_info.current_pitch = target.pitch;
    display_info.current_bits_per_pixel = target.bits_per_pixel;
    display_info.current_refresh_hz = target.refresh_hz;
    display_info.last_rollback_succeeded = true;
    return true;
}

bool nexus_display_hdr_monitor_supported(void) {
    return display_info.initialized && display_info.edid_valid &&
           display_info.hdr_static_metadata &&
           (display_info.hdr_eotf_flags & 0x0CU) != 0U;
}

bool nexus_display_hdr_output_available(void) {
    if (!nexus_display_hdr_monitor_supported() ||
        !display_backend_registered || display_backend.supports_hdr == NULL)
        return false;
    const struct nexus_display_mode current = {
        .width = display_info.current_width,
        .height = display_info.current_height,
        .pitch = display_info.current_pitch,
        .bits_per_pixel = display_info.current_bits_per_pixel,
        .refresh_hz = display_info.current_refresh_hz,
        .current_boot_mode = true,
        .firmware_listed = true
    };
    return display_backend.supports_hdr(display_backend_context, &current);
}
