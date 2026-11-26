#include "graphics.h"

#include <stddef.h>
#include <stdint.h>

struct glyph {
    char character;
    uint8_t rows[7];
};

/* Compact 5x7 bitmap font used by the early framebuffer desktop. */
static const struct glyph font[] = {
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,18,18,12}},
    {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,25,21,19,19,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {':',{0,4,4,0,4,4,0}}, {'.',{0,0,0,0,0,12,12}},
    {'-',{0,0,0,31,0,0,0}}, {'/',{1,2,2,4,8,8,16}},
    {' ',{0,0,0,0,0,0,0}}
};

static struct nexus_graphics_surface active_surface;
static bool initialized;
static struct nexus_graphics_damage pending_damage;
static bool pending_damage_valid;

static void graphics_mark_damage(uint64_t x, uint64_t y,
                                 uint64_t width, uint64_t height) {
    if (!initialized || x >= active_surface.width ||
        y >= active_surface.height || width == 0U || height == 0U) return;
    if (width > active_surface.width - x)
        width = active_surface.width - x;
    if (height > active_surface.height - y)
        height = active_surface.height - y;

    if (!pending_damage_valid) {
        pending_damage = (struct nexus_graphics_damage){x, y, width, height};
        pending_damage_valid = true;
        return;
    }
    const uint64_t left = x < pending_damage.x ? x : pending_damage.x;
    const uint64_t top = y < pending_damage.y ? y : pending_damage.y;
    const uint64_t right_a = x + width;
    const uint64_t right_b = pending_damage.x + pending_damage.width;
    const uint64_t bottom_a = y + height;
    const uint64_t bottom_b = pending_damage.y + pending_damage.height;
    const uint64_t right = right_a > right_b ? right_a : right_b;
    const uint64_t bottom = bottom_a > bottom_b ? bottom_a : bottom_b;
    pending_damage = (struct nexus_graphics_damage){
        left, top, right - left, bottom - top
    };
}

static bool mask_valid(uint8_t size, uint8_t shift) {
    return size > 0U && size <= 8U && shift < 32U &&
           (uint16_t)size + (uint16_t)shift <= 32U;
}

static uint32_t channel_mask(uint8_t size, uint8_t shift) {
    return ((1U << size) - 1U) << shift;
}

static uint32_t pack_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    const uint32_t rmax = (1U << active_surface.red_mask_size) - 1U;
    const uint32_t gmax = (1U << active_surface.green_mask_size) - 1U;
    const uint32_t bmax = (1U << active_surface.blue_mask_size) - 1U;
    const uint32_t r = ((uint32_t)red * rmax / 255U)
                       << active_surface.red_mask_shift;
    const uint32_t g = ((uint32_t)green * gmax / 255U)
                       << active_surface.green_mask_shift;
    const uint32_t b = ((uint32_t)blue * bmax / 255U)
                       << active_surface.blue_mask_shift;
    return r | g | b;
}

static uint8_t graphics_lerp_channel(uint8_t first, uint8_t last,
                                          uint64_t position,
                                          uint64_t extent) {
    if (extent <= 1U || position == 0U) return first;
    if (position >= extent - 1U) return last;
    const uint64_t denominator = extent - 1U;
    const uint64_t inverse = denominator - position;
    const uint64_t value = (uint64_t)first * inverse +
                           (uint64_t)last * position +
                           denominator / 2U;
    return (uint8_t)(value / denominator);
}

bool graphics_init(const struct nexus_graphics_surface *surface) {
    initialized = false;
    if (surface == NULL || surface->address == NULL ||
        surface->width == 0U || surface->height == 0U ||
        surface->bits_per_pixel != 32U ||
        !mask_valid(surface->red_mask_size, surface->red_mask_shift) ||
        !mask_valid(surface->green_mask_size, surface->green_mask_shift) ||
        !mask_valid(surface->blue_mask_size, surface->blue_mask_shift) ||
        surface->width > UINT64_MAX / 4U ||
        surface->pitch < surface->width * 4U ||
        (surface->pitch & 3U) != 0U || surface->pitch == 0U ||
        surface->height > UINT64_MAX / surface->pitch) {
        return false;
    }

    const uint32_t red = channel_mask(surface->red_mask_size,
                                      surface->red_mask_shift);
    const uint32_t green = channel_mask(surface->green_mask_size,
                                        surface->green_mask_shift);
    const uint32_t blue = channel_mask(surface->blue_mask_size,
                                       surface->blue_mask_shift);
    if ((red & green) != 0U || (red & blue) != 0U ||
        (green & blue) != 0U) {
        return false;
    }

    active_surface = *surface;
    pending_damage_valid = false;
    pending_damage = (struct nexus_graphics_damage){0};
    initialized = true;
    return true;
}

uint64_t graphics_width(void) {
    return initialized ? active_surface.width : 0U;
}

uint64_t graphics_height(void) {
    return initialized ? active_surface.height : 0U;
}

bool graphics_take_damage(struct nexus_graphics_damage *damage) {
    if (damage == NULL || !pending_damage_valid) return false;
    *damage = pending_damage;
    pending_damage = (struct nexus_graphics_damage){0};
    pending_damage_valid = false;
    return true;
}

void graphics_fill_gradient(
    uint64_t x, uint64_t y, uint64_t width, uint64_t height,
    uint8_t top_left_red, uint8_t top_left_green, uint8_t top_left_blue,
    uint8_t top_right_red, uint8_t top_right_green, uint8_t top_right_blue,
    uint8_t bottom_left_red, uint8_t bottom_left_green, uint8_t bottom_left_blue,
    uint8_t bottom_right_red, uint8_t bottom_right_green,
    uint8_t bottom_right_blue) {
    if (!initialized || width == 0U || height == 0U ||
        width > UINT32_MAX || height > UINT32_MAX ||
        x >= active_surface.width || y >= active_surface.height) return;

    const uint64_t gradient_width = width;
    const uint64_t gradient_height = height;
    if (width > active_surface.width - x)
        width = active_surface.width - x;
    if (height > active_surface.height - y)
        height = active_surface.height - y;
    graphics_mark_damage(x, y, width, height);

    volatile uint8_t *base = (volatile uint8_t *)active_surface.address;
    for (uint64_t row = 0U; row < height; ++row) {
        const uint8_t left_red = graphics_lerp_channel(
            top_left_red, bottom_left_red, row, gradient_height);
        const uint8_t left_green = graphics_lerp_channel(
            top_left_green, bottom_left_green, row, gradient_height);
        const uint8_t left_blue = graphics_lerp_channel(
            top_left_blue, bottom_left_blue, row, gradient_height);
        const uint8_t right_red = graphics_lerp_channel(
            top_right_red, bottom_right_red, row, gradient_height);
        const uint8_t right_green = graphics_lerp_channel(
            top_right_green, bottom_right_green, row, gradient_height);
        const uint8_t right_blue = graphics_lerp_channel(
            top_right_blue, bottom_right_blue, row, gradient_height);
        volatile uint32_t *pixels = (volatile uint32_t *)(uintptr_t)
            (base + (y + row) * active_surface.pitch + x * 4U);
        for (uint64_t column = 0U; column < width; ++column) {
            pixels[column] = pack_rgb(
                graphics_lerp_channel(left_red, right_red, column,
                                      gradient_width),
                graphics_lerp_channel(left_green, right_green, column,
                                      gradient_width),
                graphics_lerp_channel(left_blue, right_blue, column,
                                      gradient_width));
        }
    }
}

uint64_t graphics_pitch(void) {
    return initialized ? active_surface.pitch : 0U;
}

bool graphics_blit_xrgb8888(uint64_t x, uint64_t y, uint64_t width,
                            uint64_t height, const uint32_t *pixels,
                            size_t stride_pixels) {
    if (!initialized || pixels == NULL || width == 0U || height == 0U ||
        x >= active_surface.width || y >= active_surface.height ||
        stride_pixels < width ||
        width > (uint64_t)(SIZE_MAX / sizeof(uint32_t)) ||
        stride_pixels > SIZE_MAX / sizeof(uint32_t) ||
        (height - 1U) >
            (uint64_t)(((SIZE_MAX / sizeof(uint32_t)) -
                        (size_t)width) / stride_pixels)) {
        return false;
    }

    const uint64_t copy_width = width < active_surface.width - x
        ? width : active_surface.width - x;
    const uint64_t copy_height = height < active_surface.height - y
        ? height : active_surface.height - y;
    if (copy_width == 0U || copy_height == 0U ||
        copy_width > SIZE_MAX / sizeof(uint32_t)) return false;

    graphics_mark_damage(x, y, copy_width, copy_height);
    volatile uint8_t *base = (volatile uint8_t *)active_surface.address;
    for (uint64_t row = 0U; row < copy_height; ++row) {
        const uint32_t *source = pixels + (size_t)row * stride_pixels;
        volatile uint32_t *destination = (volatile uint32_t *)(void *)(
            base + (y + row) * active_surface.pitch + x * 4U);
        for (uint64_t column = 0U; column < copy_width; ++column) {
            const uint32_t pixel = source[column];
            destination[column] = pack_rgb(
                (uint8_t)(pixel >> 16), (uint8_t)(pixel >> 8),
                (uint8_t)pixel);
        }
    }
    return true;
}

void graphics_fill_rect(uint64_t x, uint64_t y, uint64_t width,
                        uint64_t height, uint8_t red, uint8_t green,
                        uint8_t blue) {
    if (!initialized || x >= active_surface.width ||
        y >= active_surface.height || width == 0U || height == 0U) return;

    const uint64_t available_width = active_surface.width - x;
    const uint64_t available_height = active_surface.height - y;
    if (width > available_width) width = available_width;
    if (height > available_height) height = available_height;

    graphics_mark_damage(x, y, width, height);
    const uint32_t color = pack_rgb(red, green, blue);
    volatile uint8_t *base = (volatile uint8_t *)active_surface.address;
    /*
     * REP STOSD uses the CPU's optimized string-store path for solid fills.
     * The active scanout is still the firmware-provided GPU framebuffer; this
     * speeds up CPU rasterization without claiming a GPU drawing engine.
     */
    for (uint64_t row = 0U; row < height; ++row) {
        uint32_t *pixels = (uint32_t *)(uintptr_t)
            (base + (y + row) * active_surface.pitch + x * 4U);
        size_t remaining = (size_t)width;
        __asm__ volatile ("cld; rep stosl"
                          : "+D"(pixels), "+c"(remaining)
                          : "a"(color)
                          : "memory", "cc");
    }
}


static uint8_t unpack_channel(uint32_t pixel, uint8_t size, uint8_t shift) {
    const uint32_t maximum = (1U << size) - 1U;
    const uint32_t value = (pixel >> shift) & maximum;
    return (uint8_t)(value * 255U / maximum);
}

static uint8_t blend_channel(uint8_t source, uint8_t destination,
                             uint8_t alpha) {
    const uint32_t inverse = 255U - alpha;
    return (uint8_t)(((uint32_t)source * alpha +
                      (uint32_t)destination * inverse + 127U) / 255U);
}

void graphics_blend_rect(uint64_t x, uint64_t y, uint64_t width,
                         uint64_t height, uint8_t red, uint8_t green,
                         uint8_t blue, uint8_t alpha) {
    if (!initialized || alpha == 0U || x >= active_surface.width ||
        y >= active_surface.height || width == 0U || height == 0U) return;
    if (width > active_surface.width - x)
        width = active_surface.width - x;
    if (height > active_surface.height - y)
        height = active_surface.height - y;

    graphics_mark_damage(x, y, width, height);
    volatile uint8_t *base = (volatile uint8_t *)active_surface.address;
    for (uint64_t row = 0U; row < height; ++row) {
        volatile uint32_t *pixels = (volatile uint32_t *)(uintptr_t)
            (base + (y + row) * active_surface.pitch + x * 4U);
        for (uint64_t column = 0U; column < width; ++column) {
            const uint32_t old = pixels[column];
            const uint8_t old_red = unpack_channel(
                old, active_surface.red_mask_size,
                active_surface.red_mask_shift);
            const uint8_t old_green = unpack_channel(
                old, active_surface.green_mask_size,
                active_surface.green_mask_shift);
            const uint8_t old_blue = unpack_channel(
                old, active_surface.blue_mask_size,
                active_surface.blue_mask_shift);
            pixels[column] = pack_rgb(
                blend_channel(red, old_red, alpha),
                blend_channel(green, old_green, alpha),
                blend_channel(blue, old_blue, alpha));
        }
    }
}



#define NEXUS_GRAPHICS_CIRCLE_MAX_RADIUS 2048U

/* Integer square root with a fixed, small search interval. Circle rasterization
 * is row-based, so the cost is bounded by 4097 scanlines rather than radius^2.
 */
static uint32_t graphics_integer_sqrt(uint32_t value) {
    uint32_t low = 0U;
    uint32_t high = NEXUS_GRAPHICS_CIRCLE_MAX_RADIUS + 1U;
    while (high - low > 1U) {
        const uint32_t middle = low + (high - low) / 2U;
        if (middle * middle <= value) low = middle;
        else high = middle;
    }
    return low;
}

static void graphics_circle_internal(
    uint64_t center_x, uint64_t center_y, uint32_t radius,
    uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha,
    bool blend) {
    if (!initialized || radius > NEXUS_GRAPHICS_CIRCLE_MAX_RADIUS ||
        (blend && alpha == 0U)) return;

    const uint64_t surface_width = active_surface.width;
    const uint64_t surface_height = active_surface.height;
    if (center_y >= surface_height &&
        center_y - surface_height >= radius) return;

    const uint64_t first_y = center_y > radius
        ? center_y - radius : 0U;
    uint64_t last_y;
    if (center_y >= surface_height) {
        last_y = surface_height - 1U;
    } else {
        const uint64_t available_below = surface_height - 1U - center_y;
        last_y = radius > available_below
            ? surface_height - 1U : center_y + radius;
    }
    if (first_y >= surface_height || first_y > last_y) return;

    const uint32_t radius_squared = radius * radius;
    for (uint64_t y = first_y; y <= last_y; ++y) {
        const uint64_t distance_y = y >= center_y
            ? y - center_y : center_y - y;
        const uint32_t dy = (uint32_t)distance_y;
        const uint32_t horizontal_squared =
            radius_squared - dy * dy;
        const uint32_t span = graphics_integer_sqrt(horizontal_squared);
        uint64_t left;
        uint64_t right;

        if (center_x >= surface_width) {
            const uint64_t distance_left =
                center_x - (surface_width - 1U);
            if (distance_left > span) continue;
            left = center_x > span ? center_x - span : 0U;
            right = surface_width - 1U;
        } else {
            left = center_x > span ? center_x - span : 0U;
            const uint64_t available_right =
                surface_width - 1U - center_x;
            right = span > available_right
                ? surface_width - 1U : center_x + span;
        }

        if (right < left) continue;
        const uint64_t row_width = right - left + 1U;
        if (blend) {
            graphics_blend_rect(left, y, row_width, 1U,
                                red, green, blue, alpha);
        } else {
            graphics_fill_rect(left, y, row_width, 1U,
                               red, green, blue);
        }
    }
}

void graphics_fill_circle(uint64_t center_x, uint64_t center_y,
                          uint32_t radius, uint8_t red, uint8_t green,
                          uint8_t blue) {
    graphics_circle_internal(center_x, center_y, radius,
                             red, green, blue, 255U, false);
}

void graphics_blend_circle(uint64_t center_x, uint64_t center_y,
                           uint32_t radius, uint8_t red, uint8_t green,
                           uint8_t blue, uint8_t alpha) {
    graphics_circle_internal(center_x, center_y, radius,
                             red, green, blue, alpha, true);
}

/*
 * Estimate 4x4 sub-pixel coverage for one pixel in a rounded rectangle's
 * corner. This keeps the high-contrast curved outline smooth without
 * allocating a backbuffer or requiring a graphics device driver.
 */
static uint8_t rounded_corner_coverage(uint32_t radius,
                                       uint64_t pixel_x,
                                       uint64_t pixel_y,
                                       uint64_t width,
                                       uint64_t height,
                                       bool right, bool bottom) {
    const uint64_t radius_q = (uint64_t)radius * 4U;
    const uint64_t center_x_q =
        (right ? width - radius : radius) * 4U;
    const uint64_t center_y_q =
        (bottom ? height - radius : radius) * 4U;
    uint8_t covered = 0U;

    for (uint32_t sy = 1U; sy <= 3U; sy += 2U) {
        const uint64_t sample_y_q = pixel_y * 4U + sy;
        const uint64_t dy = sample_y_q > center_y_q
            ? sample_y_q - center_y_q : center_y_q - sample_y_q;
        for (uint32_t sx = 1U; sx <= 3U; sx += 2U) {
            const uint64_t sample_x_q = pixel_x * 4U + sx;
            const uint64_t dx = sample_x_q > center_x_q
                ? sample_x_q - center_x_q : center_x_q - sample_x_q;
            if (dx * dx + dy * dy <= radius_q * radius_q)
                ++covered;
        }
    }
    return covered;
}

static void graphics_rounded_rect_internal(
    uint64_t x, uint64_t y, uint64_t width, uint64_t height,
    uint32_t radius, uint8_t red, uint8_t green, uint8_t blue,
    uint8_t alpha, bool blend) {
    if (!initialized || x >= active_surface.width ||
        y >= active_surface.height || width == 0U || height == 0U) return;
    if (width > active_surface.width - x)
        width = active_surface.width - x;
    if (height > active_surface.height - y)
        height = active_surface.height - y;
    if (radius > 256U) radius = 256U;
    if ((uint64_t)radius * 2U > width) radius = (uint32_t)(width / 2U);
    if ((uint64_t)radius * 2U > height) radius = (uint32_t)(height / 2U);
    if (alpha == 0U) return;
    if (radius == 0U) {
        if (blend) graphics_blend_rect(x, y, width, height,
                                       red, green, blue, alpha);
        else graphics_fill_rect(x, y, width, height, red, green, blue);
        return;
    }

    const uint64_t center_width = width - (uint64_t)radius * 2U;
    const uint8_t source_alpha = blend ? alpha : 255U;
    for (uint64_t row = 0U; row < height; ++row) {
        const bool top_corner = row < radius;
        const bool bottom_corner = row >= height - radius;
        const bool corner_row = top_corner || bottom_corner;

        if (center_width != 0U) {
            const uint64_t center_x = x + radius;
            if (blend)
                graphics_blend_rect(center_x, y + row, center_width, 1U,
                                    red, green, blue, alpha);
            else
                graphics_fill_rect(center_x, y + row, center_width, 1U,
                                   red, green, blue);
        }
        if (!corner_row) {
            if (blend)
                graphics_blend_rect(x, y + row, radius, 1U,
                                    red, green, blue, alpha);
            else
                graphics_fill_rect(x, y + row, radius, 1U,
                                   red, green, blue);
            if (blend)
                graphics_blend_rect(x + width - radius, y + row,
                                    radius, 1U, red, green, blue, alpha);
            else
                graphics_fill_rect(x + width - radius, y + row,
                                   radius, 1U, red, green, blue);
            continue;
        }

        const uint64_t corner_y = row;
        for (uint64_t column = 0U; column < radius; ++column) {
            uint8_t coverage = rounded_corner_coverage(
                radius, column, corner_y, width, height, false,
                bottom_corner);
            if (coverage != 0U) {
                const uint8_t covered_alpha = (uint8_t)(
                    ((uint16_t)source_alpha * coverage + 2U) / 4U);
                graphics_blend_rect(x + column, y + row, 1U, 1U,
                                    red, green, blue, covered_alpha);
            }

            const uint64_t right_column = width - radius + column;
            coverage = rounded_corner_coverage(
                radius, right_column, corner_y, width, height, true,
                bottom_corner);
            if (coverage != 0U) {
                const uint8_t covered_alpha = (uint8_t)(
                    ((uint16_t)source_alpha * coverage + 2U) / 4U);
                graphics_blend_rect(x + right_column, y + row, 1U, 1U,
                                    red, green, blue, covered_alpha);
            }
        }
    }
}
void graphics_round_rect(uint64_t x, uint64_t y, uint64_t width,
                         uint64_t height, uint32_t radius,
                         uint8_t red, uint8_t green, uint8_t blue) {
    graphics_rounded_rect_internal(x, y, width, height, radius,
                                   red, green, blue, 255U, false);
}

void graphics_blend_round_rect(uint64_t x, uint64_t y, uint64_t width,
                               uint64_t height, uint32_t radius,
                               uint8_t red, uint8_t green, uint8_t blue,
                               uint8_t alpha) {
    if (alpha == 0U) return;
    graphics_rounded_rect_internal(x, y, width, height, radius,
                                   red, green, blue, alpha, true);
}

void graphics_clear(uint8_t red, uint8_t green, uint8_t blue) {
    graphics_fill_rect(0U, 0U, graphics_width(), graphics_height(),
                       red, green, blue);
}

void graphics_draw_rect(uint64_t x, uint64_t y, uint64_t width,
                        uint64_t height, uint8_t red, uint8_t green,
                        uint8_t blue) {
    if (!initialized || x >= active_surface.width ||
        y >= active_surface.height || width == 0U || height == 0U) return;

    /* Clip before computing the far edges so hostile dimensions cannot wrap. */
    if (width > active_surface.width - x)
        width = active_surface.width - x;
    if (height > active_surface.height - y)
        height = active_surface.height - y;

    graphics_fill_rect(x, y, width, 1U, red, green, blue);
    if (height > 1U)
        graphics_fill_rect(x, y + height - 1U, width, 1U, red, green, blue);
    graphics_fill_rect(x, y, 1U, height, red, green, blue);
    if (width > 1U)
        graphics_fill_rect(x + width - 1U, y, 1U, height, red, green, blue);
}

static const struct glyph *find_glyph(char character) {
    if (character >= 'a' && character <= 'z')
        character = (char)(character - 'a' + 'A');
    for (size_t i = 0U; i < sizeof(font) / sizeof(font[0]); ++i) {
        if (font[i].character == character) return &font[i];
    }
    return &font[sizeof(font) / sizeof(font[0]) - 1U];
}

void graphics_draw_text(uint64_t x, uint64_t y, const char *text,
                        uint32_t scale, uint8_t red, uint8_t green,
                        uint8_t blue) {
    if (!initialized || text == NULL || scale == 0U ||
        x >= active_surface.width || y >= active_surface.height) return;

    const uint32_t color = pack_rgb(red, green, blue);
    volatile uint8_t *base = (volatile uint8_t *)active_surface.address;
    const uint64_t max_advance = active_surface.width - x;
    const uint64_t max_height = active_surface.height - y;

    for (size_t index = 0U; text[index] != '\0'; ++index) {
        /* Check multiplication before calculating the character position. */
        if ((uint64_t)index > UINT64_MAX / (6U * (uint64_t)scale)) break;
        const uint64_t advance = (uint64_t)index * 6U * (uint64_t)scale;
        if (advance >= max_advance) break;
        graphics_mark_damage(x + advance, y, 5U * (uint64_t)scale,
                             7U * (uint64_t)scale);
        const struct glyph *glyph = find_glyph(text[index]);

        for (uint32_t gy = 0U; gy < 7U; ++gy) {
            const uint64_t glyph_y = (uint64_t)gy * scale;
            if (glyph_y >= max_height) continue;
            for (uint32_t gx = 0U; gx < 5U; ++gx) {
                if ((glyph->rows[gy] & (1U << (4U - gx))) == 0U) continue;
                const uint64_t glyph_x = advance + (uint64_t)gx * scale;
                if (glyph_x >= max_advance) continue;
                for (uint32_t sy = 0U; sy < scale; ++sy) {
                    const uint64_t py_offset = glyph_y + sy;
                    if (py_offset >= max_height) break;
                    for (uint32_t sx = 0U; sx < scale; ++sx) {
                        const uint64_t px_offset = glyph_x + sx;
                        if (px_offset >= max_advance) break;
                        volatile uint32_t *pixel = (volatile uint32_t *)(uintptr_t)
                            (base + (y + py_offset) * active_surface.pitch +
                             (x + px_offset) * 4U);
                        *pixel = color;
                    }
                }
            }
        }
    }
}

/* Blend a single antialiased glyph pixel. The destination is kept in the
 * framebuffer's declared channel layout, just like the other primitives.
 */
static void graphics_blend_text_pixel(uint64_t x, uint64_t y,
                                      uint8_t red, uint8_t green,
                                      uint8_t blue, uint8_t alpha) {
    if (alpha == 0U || x >= active_surface.width ||
        y >= active_surface.height) return;
    volatile uint8_t *base = (volatile uint8_t *)active_surface.address;
    volatile uint32_t *pixel = (volatile uint32_t *)(uintptr_t)
        (base + y * active_surface.pitch + x * 4U);
    const uint32_t old = *pixel;
    *pixel = pack_rgb(
        blend_channel(red, unpack_channel(old,
            active_surface.red_mask_size, active_surface.red_mask_shift), alpha),
        blend_channel(green, unpack_channel(old,
            active_surface.green_mask_size, active_surface.green_mask_shift), alpha),
        blend_channel(blue, unpack_channel(old,
            active_surface.blue_mask_size, active_surface.blue_mask_shift), alpha));
}

/* The 5x7 glyph bitmap is scaled as fractional rectangles. Pixel coverage is
 * calculated from the exact overlap with each destination pixel, which keeps
 * fractional font sizes smooth rather than stretching jagged integer blocks.
 */
void graphics_draw_text_aa(uint64_t x, uint64_t y, const char *text,
                           uint32_t scale_16_16, uint8_t red,
                           uint8_t green, uint8_t blue) {
    const uint32_t one = 1U << 16;
    const uint32_t maximum_scale = 4U << 16;
    if (!initialized || text == NULL || x >= active_surface.width ||
        y >= active_surface.height || scale_16_16 < one ||
        scale_16_16 > maximum_scale ||
        active_surface.width > (UINT64_MAX >> 16) ||
        active_surface.height > (UINT64_MAX >> 16)) return;

    const uint64_t max_width = active_surface.width - x;
    const uint64_t max_height = active_surface.height - y;
    const uint64_t max_width_fp = max_width << 16;
    const uint64_t max_height_fp = max_height << 16;

    for (size_t index = 0U; text[index] != '\0'; ++index) {
        if ((uint64_t)index >
            (UINT64_MAX / scale_16_16 - 5U) / 6U) break;
        const uint64_t advance_fp =
            ((uint64_t)index * 6U) * scale_16_16;
        if (advance_fp >= max_width_fp) break;
        const uint64_t glyph_width =
            ((5ULL * scale_16_16) + 0xFFFFU) >> 16;
        const uint64_t glyph_height =
            ((7ULL * scale_16_16) + 0xFFFFU) >> 16;
        graphics_mark_damage(x + (advance_fp >> 16), y,
                             glyph_width, glyph_height);
        const struct glyph *glyph = find_glyph(text[index]);

        for (uint32_t gy = 0U; gy < 7U; ++gy) {
            const uint64_t top_fp = (uint64_t)gy * scale_16_16;
            if (top_fp >= max_height_fp) break;
            const uint64_t bottom_fp = top_fp + scale_16_16;
            const uint64_t first_y = top_fp >> 16;
            uint64_t last_y = (bottom_fp - 1U) >> 16;
            if (last_y >= max_height) last_y = max_height - 1U;

            for (uint32_t gx = 0U; gx < 5U; ++gx) {
                if ((glyph->rows[gy] & (1U << (4U - gx))) == 0U)
                    continue;
                const uint64_t left_fp =
                    advance_fp + (uint64_t)gx * scale_16_16;
                if (left_fp >= max_width_fp) continue;
                const uint64_t right_fp = left_fp + scale_16_16;
                const uint64_t first_x = left_fp >> 16;
                uint64_t last_x = (right_fp - 1U) >> 16;
                if (last_x >= max_width) last_x = max_width - 1U;

                for (uint64_t py = first_y; py <= last_y; ++py) {
                    const uint64_t pixel_top = py << 16;
                    const uint64_t pixel_bottom = pixel_top + one;
                    const uint64_t overlap_y =
                        (bottom_fp < pixel_bottom ? bottom_fp : pixel_bottom) -
                        (top_fp > pixel_top ? top_fp : pixel_top);
                    for (uint64_t px = first_x; px <= last_x; ++px) {
                        const uint64_t pixel_left = px << 16;
                        const uint64_t pixel_right = pixel_left + one;
                        const uint64_t overlap_x =
                            (right_fp < pixel_right ? right_fp : pixel_right) -
                            (left_fp > pixel_left ? left_fp : pixel_left);
                        const uint64_t area = overlap_x * overlap_y;
                        const uint8_t alpha = (uint8_t)(
                            (area * 255U + (1ULL << 31)) >> 32);
                        graphics_blend_text_pixel(x + px, y + py,
                                                  red, green, blue, alpha);
                    }
                }
            }
        }
    }
}
