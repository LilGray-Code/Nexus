#include "display_edid.h"

#include <stdint.h>

static bool edid_checksum_valid(const uint8_t *block) {
    uint8_t sum = 0U;
    for (size_t i = 0U; i < NEXUS_EDID_BLOCK_SIZE; ++i)
        sum = (uint8_t)(sum + block[i]);
    return sum == 0U;
}

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static bool edid_header_valid(const uint8_t *bytes) {
    static const uint8_t signature[8] = {
        0x00U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x00U
    };
    for (size_t i = 0U; i < sizeof(signature); ++i)
        if (bytes[i] != signature[i]) return false;
    return true;
}

static bool parse_detailed_timing(const uint8_t *dtd,
                                  struct nexus_display_mode *mode) {
    const uint32_t pixel_clock_10khz = read_le16(dtd);
    if (pixel_clock_10khz == 0U) return false;

    const uint32_t horizontal_active =
        (uint32_t)dtd[2] | ((uint32_t)(dtd[4] & 0xF0U) << 4);
    const uint32_t horizontal_blank =
        (uint32_t)dtd[3] | ((uint32_t)(dtd[4] & 0x0FU) << 8);
    const uint32_t vertical_active =
        (uint32_t)dtd[5] | ((uint32_t)(dtd[7] & 0xF0U) << 4);
    const uint32_t vertical_blank =
        (uint32_t)dtd[6] | ((uint32_t)(dtd[7] & 0x0FU) << 8);

    if (horizontal_active == 0U || vertical_active == 0U ||
        horizontal_blank == 0U || vertical_blank == 0U) return false;

    const uint32_t horizontal_total =
        horizontal_active + horizontal_blank;
    const uint32_t vertical_total = vertical_active + vertical_blank;
    if (horizontal_total < horizontal_active ||
        vertical_total < vertical_active) return false;

    const uint64_t pixel_clock_hz =
        (uint64_t)pixel_clock_10khz * 10000ULL;
    const uint64_t denominator =
        (uint64_t)horizontal_total * vertical_total;
    if (denominator == 0U || pixel_clock_hz > UINT64_MAX / 1000ULL)
        return false;

    const uint64_t refresh_millihz =
        (pixel_clock_hz * 1000ULL + denominator / 2ULL) / denominator;
    if (refresh_millihz == 0U || refresh_millihz > UINT32_MAX)
        return false;

    *mode = (struct nexus_display_mode){
        .width = horizontal_active,
        .height = vertical_active,
        .horizontal_total = horizontal_total,
        .vertical_total = vertical_total,
        .pixel_clock_khz = pixel_clock_10khz * 10U,
        .refresh_millihz = (uint32_t)refresh_millihz,
        .interlaced = (dtd[17] & 0x80U) != 0U
    };
    return true;
}

bool nexus_edid_parse(const uint8_t *bytes, size_t length,
                      struct nexus_edid_info *info) {
    if (bytes == NULL || info == NULL || length < NEXUS_EDID_BLOCK_SIZE ||
        !edid_header_valid(bytes) || !edid_checksum_valid(bytes) ||
        bytes[18] != 1U || bytes[19] > 4U) return false;

    const uint8_t extensions = bytes[126];
    if (extensions > NEXUS_EDID_MAX_EXTENSIONS) return false;
    const size_t required_blocks = (size_t)extensions + 1U;
    if (required_blocks > SIZE_MAX / NEXUS_EDID_BLOCK_SIZE ||
        length < required_blocks * NEXUS_EDID_BLOCK_SIZE) return false;

    /* Validate every extension even though extension timing formats are not
     * interpreted in this initial implementation.
     */
    for (size_t i = 1U; i < required_blocks; ++i) {
        if (!edid_checksum_valid(bytes + i * NEXUS_EDID_BLOCK_SIZE))
            return false;
    }

    struct nexus_edid_info parsed = {
        .manufacturer_id = (uint16_t)(
            ((uint16_t)bytes[8] << 8) | bytes[9]),
        .product_code = read_le16(bytes + 10U),
        .serial_number = (uint32_t)bytes[12] |
            ((uint32_t)bytes[13] << 8) |
            ((uint32_t)bytes[14] << 16) |
            ((uint32_t)bytes[15] << 24),
        .version = bytes[18],
        .revision = bytes[19],
        .extension_count = extensions,
        .mode_count = 0U,
        .preferred_mode_index = -1
    };

    for (size_t i = 0U; i < NEXUS_EDID_MAX_BASE_MODES; ++i) {
        struct nexus_display_mode mode;
        const uint8_t *descriptor = bytes + 54U + i * 18U;
        if (!parse_detailed_timing(descriptor, &mode)) continue;
        if (parsed.mode_count >= NEXUS_EDID_MAX_BASE_MODES) return false;
        parsed.modes[parsed.mode_count] = mode;
        ++parsed.mode_count;
        if (i == 0U && (bytes[24] & 0x02U) != 0U)
            parsed.preferred_mode_index = (int8_t)(parsed.mode_count - 1U);
    }

    *info = parsed;
    return true;
}
