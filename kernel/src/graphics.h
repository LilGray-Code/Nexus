#ifndef NEXUS_GRAPHICS_H
#define NEXUS_GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>

struct nexus_graphics_surface {
    volatile void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bits_per_pixel;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

bool graphics_init(const struct nexus_graphics_surface *surface);
uint64_t graphics_width(void);
uint64_t graphics_height(void);
void graphics_clear(uint8_t red, uint8_t green, uint8_t blue);
void graphics_fill_rect(uint64_t x, uint64_t y, uint64_t width,
                        uint64_t height, uint8_t red, uint8_t green,
                        uint8_t blue);
void graphics_draw_rect(uint64_t x, uint64_t y, uint64_t width,
                        uint64_t height, uint8_t red, uint8_t green,
                        uint8_t blue);
/* Alpha-blend an RGB color over the clipped destination rectangle. */
void graphics_blend_rect(uint64_t x, uint64_t y, uint64_t width,
                         uint64_t height, uint8_t red, uint8_t green,
                         uint8_t blue, uint8_t alpha);
/* Circle radius is bounded to 2048 pixels; fully off-screen rows are skipped. */
void graphics_fill_circle(uint64_t center_x, uint64_t center_y,
                          uint32_t radius, uint8_t red, uint8_t green,
                          uint8_t blue);
void graphics_blend_circle(uint64_t center_x, uint64_t center_y,
                           uint32_t radius, uint8_t red, uint8_t green,
                           uint8_t blue, uint8_t alpha);
/* Rounded rectangles use a bounded radius (maximum 256 pixels). */
void graphics_round_rect(uint64_t x, uint64_t y, uint64_t width,
                          uint64_t height, uint32_t radius,
                          uint8_t red, uint8_t green, uint8_t blue);
void graphics_blend_round_rect(uint64_t x, uint64_t y, uint64_t width,
                               uint64_t height, uint32_t radius,
                               uint8_t red, uint8_t green, uint8_t blue,
                               uint8_t alpha);
void graphics_draw_text(uint64_t x, uint64_t y, const char *text,
                        uint32_t scale, uint8_t red, uint8_t green,
                        uint8_t blue);
/* Draw the built-in 5x7 font with 16.16 fractional scale and per-pixel
 * area coverage. Supported scale range is 1.0x through 4.0x.
 */
void graphics_draw_text_aa(uint64_t x, uint64_t y, const char *text,
                           uint32_t scale_16_16, uint8_t red,
                           uint8_t green, uint8_t blue);

#endif
