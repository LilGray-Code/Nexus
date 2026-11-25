#include "graphics.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    uint32_t framebuffer[5U * 3U] = {0U};
    const struct nexus_graphics_surface surface = {
        .address = framebuffer,
        .width = 4U,
        .height = 3U,
        .pitch = 5U * sizeof(uint32_t),
        .bits_per_pixel = 32U,
        .red_mask_size = 8U,
        .red_mask_shift = 16U,
        .green_mask_size = 8U,
        .green_mask_shift = 8U,
        .blue_mask_size = 8U,
        .blue_mask_shift = 0U
    };
    assert(graphics_init(&surface));
    assert(graphics_width() == 4U);
    assert(graphics_height() == 3U);
    assert(graphics_pitch() == 5U * sizeof(uint32_t));

    const uint32_t source[8] = {
        0x00FF0000U, 0x0000FF00U, 0x000000FFU, 0U,
        0x00FFFFFFU, 0x0000FFFFU, 0x00FF00FFU, 0U
    };
    assert(graphics_blit_xrgb8888(2U, 1U, 3U, 2U, source, 4U));
    assert(framebuffer[1U * 5U + 2U] == 0x00FF0000U);
    assert(framebuffer[1U * 5U + 3U] == 0x0000FF00U);
    assert(framebuffer[2U * 5U + 2U] == 0x00FFFFFFU);
    assert(framebuffer[2U * 5U + 3U] == 0x0000FFFFU);
    assert(framebuffer[1U * 5U + 1U] == 0U); /* Left guard. */
    assert(framebuffer[1U * 5U + 4U] == 0U); /* Row padding. */

    struct nexus_graphics_damage damage;
    assert(graphics_take_damage(&damage));
    assert(damage.x == 2U && damage.y == 1U);
    assert(damage.width == 2U && damage.height == 2U);
    assert(!graphics_take_damage(&damage));
    assert(!graphics_blit_xrgb8888(0U, 0U, 0U, 1U, source, 4U));
    assert(!graphics_blit_xrgb8888(0U, 0U, 2U, 1U, source, 1U));
    assert(!graphics_blit_xrgb8888(0U, 0U, 1U, 3U, source,
                                   SIZE_MAX / sizeof(uint32_t)));

    puts("NEXUS GRAPHICS BLIT TESTS PASSED");
    return 0;
}
