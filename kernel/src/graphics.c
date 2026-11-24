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
    initialized = true;
    return true;
}

uint64_t graphics_width(void) {
    return initialized ? active_surface.width : 0U;
}

uint64_t graphics_height(void) {
    return initialized ? active_surface.height : 0U;
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

    const uint32_t color = pack_rgb(red, green, blue);
    volatile uint8_t *base = (volatile uint8_t *)active_surface.address;
    for (uint64_t row = 0U; row < height; ++row) {
        volatile uint32_t *pixels = (volatile uint32_t *)(uintptr_t)
            (base + (y + row) * active_surface.pitch + x * 4U);
        for (uint64_t column = 0U; column < width; ++column) {
            pixels[column] = color;
        }
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

static uint32_t rounded_row_inset(uint32_t radius, uint32_t distance) {
    if (radius == 0U || distance >= radius) return 0U;
    const uint32_t radius_squared = radius * radius;
    const uint32_t distance_squared = distance * distance;
    uint32_t inset = radius;
    while (inset != 0U &&
           inset * inset + distance_squared > radius_squared) {
        --inset;
    }
    return radius - inset;
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
    if (radius == 0U) {
        if (blend) graphics_blend_rect(x, y, width, height,
                                       red, green, blue, alpha);
        else graphics_fill_rect(x, y, width, height, red, green, blue);
        return;
    }

    for (uint64_t row = 0U; row < height; ++row) {
        uint32_t distance = 0U;
        if (row < radius) distance = radius - (uint32_t)row - 1U;
        else if (row >= height - radius)
            distance = (uint32_t)(row - (height - radius));
        const uint32_t inset = rounded_row_inset(radius, distance);
        const uint64_t span = width > (uint64_t)inset * 2U
            ? width - (uint64_t)inset * 2U : 0U;
        if (span == 0U) continue;
        if (blend) graphics_blend_rect(x + inset, y + row, span, 1U,
                                       red, green, blue, alpha);
        else graphics_fill_rect(x + inset, y + row, span, 1U,
                                red, green, blue);
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
