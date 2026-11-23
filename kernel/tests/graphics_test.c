#include "graphics.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t pixel_at(uint32_t x, uint32_t y) {
    uint32_t pixel = 0U;
    assert(graphics_read_pixel(x, y, &pixel));
    return pixel;
}

int main(void) {
    uint32_t framebuffer[64U * 32U] = {0};
    const struct nexus_graphics_surface surface = {
        .address = framebuffer,
        .width = 64U,
        .height = 32U,
        .pitch = 64U * 4U,
        .bits_per_pixel = 32U,
        .red_mask_size = 8U, .red_mask_shift = 16U,
        .green_mask_size = 8U, .green_mask_shift = 8U,
        .blue_mask_size = 8U, .blue_mask_shift = 0U
    };
    assert(graphics_init(&surface));
    assert(graphics_width() == 64U && graphics_height() == 32U);

    graphics_clear(0U, 0U, 0U);
    graphics_draw_text(0U, 0U, "a", 1U, 255U, 255U, 255U);
    assert(pixel_at(1U, 0U) == 0U);
    assert(pixel_at(1U, 2U) == 0x00FFFFFFU);

    graphics_clear(0U, 0U, 0U);
    graphics_draw_text(0U, 0U, "A", 1U, 255U, 255U, 255U);
    assert(pixel_at(1U, 0U) == 0x00FFFFFFU);

    graphics_clear(0U, 0U, 0U);
    graphics_draw_text_smooth(16U, 0U, "a", 2U, 255U, 255U, 255U);
    bool found_antialiased_edge = false;
    bool found_solid_foreground = false;
    for (uint32_t y = 0U; y < 14U; ++y) {
        for (uint32_t x = 16U; x < 28U; ++x) {
            const uint32_t pixel = pixel_at(x, y);
            if (pixel != 0U && pixel != 0x00FFFFFFU)
                found_antialiased_edge = true;
            if (pixel == 0x00FFFFFFU) found_solid_foreground = true;
        }
    }
    assert(found_antialiased_edge);
    assert(found_solid_foreground);

    puts("NEXUS FRAMEBUFFER GRAPHICS TESTS PASSED");
    return 0;
}
