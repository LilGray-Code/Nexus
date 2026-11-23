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
bool graphics_read_pixel(uint64_t x, uint64_t y, uint32_t *pixel);
bool graphics_write_pixel(uint64_t x, uint64_t y, uint32_t pixel);
bool graphics_write_rgb_pixel(uint64_t x, uint64_t y, uint8_t red,
                              uint8_t green, uint8_t blue);
void graphics_clear(uint8_t red, uint8_t green, uint8_t blue);
void graphics_fill_rect(uint64_t x, uint64_t y, uint64_t width,
                        uint64_t height, uint8_t red, uint8_t green,
                        uint8_t blue);
void graphics_draw_rect(uint64_t x, uint64_t y, uint64_t width,
                        uint64_t height, uint8_t red, uint8_t green,
                        uint8_t blue);
void graphics_fill_gradient_rect(uint64_t x, uint64_t y,
                                 uint64_t width, uint64_t height,
                                 uint8_t top_red, uint8_t top_green,
                                 uint8_t top_blue, uint8_t bottom_red,
                                 uint8_t bottom_green, uint8_t bottom_blue);
void graphics_fill_round_rect(uint64_t x, uint64_t y, uint64_t width,
                              uint64_t height, uint64_t radius,
                              uint8_t red, uint8_t green, uint8_t blue);
void graphics_draw_round_rect(uint64_t x, uint64_t y, uint64_t width,
                              uint64_t height, uint64_t radius,
                              uint8_t red, uint8_t green, uint8_t blue);
void graphics_draw_text_smooth(uint64_t x, uint64_t y, const char *text,
                               uint32_t scale, uint8_t red, uint8_t green,
                               uint8_t blue);
void graphics_draw_text(uint64_t x, uint64_t y, const char *text,
                        uint32_t scale, uint8_t red, uint8_t green,
                        uint8_t blue);

#endif
