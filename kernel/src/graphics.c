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
    return size > 0 && size <= 8 && shift < 32 &&
           (uint16_t)size + (uint16_t)shift <= 32;
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
        surface->width == 0 || surface->height == 0 ||
        surface->bits_per_pixel != 32 ||
        !mask_valid(surface->red_mask_size, surface->red_mask_shift) ||
        !mask_valid(surface->green_mask_size, surface->green_mask_shift) ||
        !mask_valid(surface->blue_mask_size, surface->blue_mask_shift) ||
        surface->width > UINT64_MAX / 4U ||
        surface->pitch < surface->width * 4U ||
        surface->height > UINT64_MAX / surface->pitch) {
        return false;
    }

    active_surface = *surface;
    initialized = true;
    return true;
}

uint64_t graphics_width(void) {
    return initialized ? active_surface.width : 0;
}

uint64_t graphics_height(void) {
    return initialized ? active_surface.height : 0;
}

void graphics_fill_rect(uint64_t x, uint64_t y, uint64_t width,
                        uint64_t height, uint8_t red, uint8_t green,
                        uint8_t blue) {
    if (!initialized || x >= active_surface.width ||
        y >= active_surface.height || width == 0 || height == 0) return;

    const uint64_t available_width = active_surface.width - x;
    const uint64_t available_height = active_surface.height - y;
    if (width > available_width) width = available_width;
    if (height > available_height) height = available_height;

    const uint32_t color = pack_rgb(red, green, blue);
    volatile uint8_t *base = (volatile uint8_t *)active_surface.address;
    for (uint64_t row = 0; row < height; ++row) {
        volatile uint32_t *pixels = (volatile uint32_t *)(uintptr_t)
            (base + (y + row) * active_surface.pitch + x * 4U);
        for (uint64_t column = 0; column < width; ++column) {
            pixels[column] = color;
        }
    }
}

bool graphics_read_pixel(uint64_t x, uint64_t y, uint32_t *pixel) {
    if (!initialized || pixel == NULL || x >= active_surface.width ||
        y >= active_surface.height) return false;
    const volatile uint32_t *address = (const volatile uint32_t *)(uintptr_t)
        ((const volatile uint8_t *)active_surface.address +
         y * active_surface.pitch + x * 4U);
    *pixel = *address;
    return true;
}

bool graphics_write_pixel(uint64_t x, uint64_t y, uint32_t pixel) {
    if (!initialized || x >= active_surface.width ||
        y >= active_surface.height) return false;
    volatile uint32_t *address = (volatile uint32_t *)(uintptr_t)
        ((volatile uint8_t *)active_surface.address +
         y * active_surface.pitch + x * 4U);
    *address = pixel;
    return true;
}

bool graphics_write_rgb_pixel(uint64_t x, uint64_t y, uint8_t red,
                              uint8_t green, uint8_t blue) {
    return graphics_write_pixel(x, y, pack_rgb(red, green, blue));
}

void graphics_clear(uint8_t red, uint8_t green, uint8_t blue) {
    graphics_fill_rect(0, 0, graphics_width(), graphics_height(),
                       red, green, blue);
}

void graphics_draw_rect(uint64_t x, uint64_t y, uint64_t width,
                        uint64_t height, uint8_t red, uint8_t green,
                        uint8_t blue) {
    if (width == 0 || height == 0) return;
    graphics_fill_rect(x, y, width, 1, red, green, blue);
    if (height > 1) graphics_fill_rect(x, y + height - 1U, width, 1,
                                       red, green, blue);
    graphics_fill_rect(x, y, 1, height, red, green, blue);
    if (width > 1) graphics_fill_rect(x + width - 1U, y, 1, height,
                                      red, green, blue);
}

/* High-quality shell primitives. Gradient rows are written in one bounded
 * pass rather than issuing a framebuffer operation for every pixel. */
void graphics_fill_gradient_rect(uint64_t x, uint64_t y,
                                 uint64_t width, uint64_t height,
                                 uint8_t top_red, uint8_t top_green,
                                 uint8_t top_blue, uint8_t bottom_red,
                                 uint8_t bottom_green, uint8_t bottom_blue) {
    if (!initialized || width == 0U || height == 0U ||
        x >= active_surface.width || y >= active_surface.height) return;
    if (width > active_surface.width - x) width = active_surface.width - x;
    if (height > active_surface.height - y) height = active_surface.height - y;
    volatile uint8_t *base = (volatile uint8_t *)active_surface.address;
    const uint64_t denominator = height > 1U ? height - 1U : 1U;
    for (uint64_t row = 0U; row < height; ++row) {
        const uint64_t rr = (uint64_t)top_red * (denominator - (row < denominator ? row : denominator)) +
                            (uint64_t)bottom_red * (row < denominator ? row : denominator);
        const uint64_t gg = (uint64_t)top_green * (denominator - (row < denominator ? row : denominator)) +
                            (uint64_t)bottom_green * (row < denominator ? row : denominator);
        const uint64_t bb = (uint64_t)top_blue * (denominator - (row < denominator ? row : denominator)) +
                            (uint64_t)bottom_blue * (row < denominator ? row : denominator);
        const uint32_t color = pack_rgb((uint8_t)(rr / denominator),
                                        (uint8_t)(gg / denominator),
                                        (uint8_t)(bb / denominator));
        volatile uint32_t *pixels = (volatile uint32_t *)(uintptr_t)
            (base + (y + row) * active_surface.pitch + x * 4U);
        for (uint64_t column = 0U; column < width; ++column)
            pixels[column] = color;
    }
}

static uint64_t round_rect_inset(uint64_t row, uint64_t height,
                                 uint64_t radius) {
    uint64_t distance = 0U;
    if (row < radius) distance = radius - row;
    else if (row >= height - radius) distance = row - (height - radius - 1U);
    if (distance == 0U) return 0U;
    const uint64_t radius_squared = radius * radius;
    const uint64_t distance_squared = distance * distance;
    uint64_t inset = 0U;
    while (inset < radius && inset * inset + distance_squared >
           radius_squared) ++inset;
    return inset;
}

void graphics_fill_round_rect(uint64_t x, uint64_t y, uint64_t width,
                              uint64_t height, uint64_t radius,
                              uint8_t red, uint8_t green, uint8_t blue) {
    if (!initialized || width == 0U || height == 0U ||
        x >= active_surface.width || y >= active_surface.height) return;
    if (width > active_surface.width - x) width = active_surface.width - x;
    if (height > active_surface.height - y) height = active_surface.height - y;
    const uint64_t max_radius = (width < height ? width : height) / 2U;
    if (radius > max_radius) radius = max_radius;
    if (radius == 0U) {
        graphics_fill_rect(x, y, width, height, red, green, blue);
        return;
    }
    for (uint64_t row = 0U; row < height; ++row) {
        const uint64_t inset = round_rect_inset(row, height, radius);
        if (width > inset * 2U)
            graphics_fill_rect(x + inset, y + row, width - inset * 2U,
                               1U, red, green, blue);
    }
}

void graphics_draw_round_rect(uint64_t x, uint64_t y, uint64_t width,
                              uint64_t height, uint64_t radius,
                              uint8_t red, uint8_t green, uint8_t blue) {
    if (!initialized || width == 0U || height == 0U ||
        x >= active_surface.width || y >= active_surface.height) return;
    if (width > active_surface.width - x) width = active_surface.width - x;
    if (height > active_surface.height - y) height = active_surface.height - y;
    const uint64_t max_radius = (width < height ? width : height) / 2U;
    if (radius > max_radius) radius = max_radius;
    if (radius == 0U) {
        graphics_draw_rect(x, y, width, height, red, green, blue);
        return;
    }
    for (uint64_t row = 0U; row < height; ++row) {
        const uint64_t inset = round_rect_inset(row, height, radius);
        if (width <= inset * 2U) continue;
        graphics_write_rgb_pixel(x + inset, y + row, red, green, blue);
        graphics_write_rgb_pixel(x + width - inset - 1U, y + row,
                                 red, green, blue);
    }
    for (uint64_t column = radius; column < width - radius; ++column) {
        graphics_write_rgb_pixel(x + column, y, red, green, blue);
        graphics_write_rgb_pixel(x + column, y + height - 1U,
                                 red, green, blue);
    }
}

static const struct glyph *find_glyph(char character) {
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }
    for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); ++i) {
        if (font[i].character == character) return &font[i];
    }
    return &font[sizeof(font) / sizeof(font[0]) - 1U];
}

void graphics_draw_text(uint64_t x, uint64_t y, const char *text,
                        uint32_t scale, uint8_t red, uint8_t green,
                        uint8_t blue) {
    if (!initialized || text == NULL || scale == 0) return;
    const uint32_t color = pack_rgb(red, green, blue);
    volatile uint8_t *base = (volatile uint8_t *)active_surface.address;

    for (size_t index = 0; text[index] != '\0'; ++index) {
        const struct glyph *glyph = find_glyph(text[index]);
        for (uint32_t gy = 0; gy < 7; ++gy) {
            for (uint32_t gx = 0; gx < 5; ++gx) {
                if ((glyph->rows[gy] & (1U << (4U - gx))) == 0) continue;
                for (uint32_t sy = 0; sy < scale; ++sy) {
                    const uint64_t py = y + (uint64_t)gy * scale + sy;
                    if (py >= active_surface.height) continue;
                    for (uint32_t sx = 0; sx < scale; ++sx) {
                        const uint64_t px = x +
                            ((uint64_t)index * 6U + gx) * scale + sx;
                        if (px >= active_surface.width) continue;
                        volatile uint32_t *pixel = (volatile uint32_t *)(uintptr_t)
                            (base + py * active_surface.pitch + px * 4U);
                        *pixel = color;
                    }
                }
            }
        }
    }
}
