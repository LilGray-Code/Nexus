#include "graphics.h"

#include <assert.h>
#include <stdint.h>

static void test_xrgb_readback_and_bounds(void) {
    uint32_t framebuffer[4] = {0U, 0U, 0U, 0U};
    const struct nexus_graphics_surface surface = {
        .address = framebuffer,
        .width = 2U,
        .height = 2U,
        .pitch = 2U * sizeof(uint32_t),
        .bits_per_pixel = 32U,
        .red_mask_size = 8U,
        .red_mask_shift = 16U,
        .green_mask_size = 8U,
        .green_mask_shift = 8U,
        .blue_mask_size = 8U,
        .blue_mask_shift = 0U
    };
    assert(graphics_init(&surface));
    graphics_fill_rect(0U, 0U, 2U, 2U, 12U, 80U, 210U);

    uint32_t pixels[4] = {0U};
    assert(graphics_read_xrgb8888(0U, 0U, 2U, 2U, pixels, 2U));
    for (size_t i = 0U; i < 4U; ++i)
        assert(pixels[i] == 0x000C50D2U);

    assert(!graphics_read_xrgb8888(1U, 0U, 2U, 1U, pixels, 2U));
    assert(!graphics_read_xrgb8888(0U, 1U, 1U, 2U, pixels, 1U));
    assert(!graphics_read_xrgb8888(0U, 0U, 2U, 1U, pixels, 1U));
    assert(!graphics_read_xrgb8888(0U, 0U, 1U, 1U, NULL, 1U));
    assert(!graphics_read_xrgb8888(0U, 0U, 0U, 1U, pixels, 1U));
}

static void test_channel_mask_conversion(void) {
    uint32_t framebuffer[1] = {0U};
    const struct nexus_graphics_surface surface = {
        .address = framebuffer,
        .width = 1U,
        .height = 1U,
        .pitch = sizeof(uint32_t),
        .bits_per_pixel = 32U,
        .red_mask_size = 8U,
        .red_mask_shift = 0U,
        .green_mask_size = 8U,
        .green_mask_shift = 8U,
        .blue_mask_size = 8U,
        .blue_mask_shift = 16U
    };
    assert(graphics_init(&surface));
    graphics_fill_rect(0U, 0U, 1U, 1U, 200U, 100U, 40U);

    uint32_t pixel = 0U;
    assert(graphics_read_xrgb8888(0U, 0U, 1U, 1U, &pixel, 1U));
    assert(pixel == 0x00C86428U);
}

int main(void) {
    test_xrgb_readback_and_bounds();
    test_channel_mask_conversion();
    return 0;
}
