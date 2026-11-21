#include "desktop.h"

#include "graphics.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

static uint64_t clamp_min(uint64_t value, uint64_t minimum) {
    return value < minimum ? minimum : value;
}

bool desktop_show(const struct limine_framebuffer *framebuffer) {
    if (framebuffer == NULL || framebuffer->address == NULL ||
        framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB) {
        serial_write("DESKTOP: no compatible RGB framebuffer; keeping serial console\r\n");
        return false;
    }

    const struct nexus_graphics_surface surface = {
        .address = framebuffer->address,
        .width = framebuffer->width,
        .height = framebuffer->height,
        .pitch = framebuffer->pitch,
        .bits_per_pixel = framebuffer->bpp,
        .red_mask_size = framebuffer->red_mask_size,
        .red_mask_shift = framebuffer->red_mask_shift,
        .green_mask_size = framebuffer->green_mask_size,
        .green_mask_shift = framebuffer->green_mask_shift,
        .blue_mask_size = framebuffer->blue_mask_size,
        .blue_mask_shift = framebuffer->blue_mask_shift
    };

    if (!graphics_init(&surface)) {
        serial_write("DESKTOP: framebuffer validation failed; keeping serial console\r\n");
        return false;
    }

    const uint64_t width = graphics_width();
    const uint64_t height = graphics_height();
    const uint64_t top_bar = clamp_min(height / 16U, 34U);
    const uint64_t bottom_bar = clamp_min(height / 11U, 48U);

    /* Deep blue Nexus desktop background with a subtle vertical gradient. */
    for (uint64_t y = 0; y < height; ++y) {
        const uint8_t red = (uint8_t)(7U + (y * 7U / height));
        const uint8_t green = (uint8_t)(16U + (y * 23U / height));
        const uint8_t blue = (uint8_t)(34U + (y * 34U / height));
        graphics_fill_rect(0, y, width, 1, red, green, blue);
    }

    graphics_fill_rect(0, 0, width, top_bar, 11, 20, 39);
    graphics_fill_rect(0, top_bar - 2U, width, 2, 27, 183, 209);
    graphics_draw_text(22, top_bar / 2U - 4U, "NEXUS", 1, 229, 246, 255);
    graphics_draw_text(width > 190U ? width - 170U : 8U,
                       top_bar / 2U - 4U, "SYSTEM READY", 1,
                       119, 229, 211);

    /* Main welcome window. Keep generous margins on small framebuffers. */
    uint64_t panel_width = width * 3U / 4U;
    uint64_t panel_height = height * 53U / 100U;
    if (panel_width > 920U) panel_width = 920U;
    if (panel_height > 480U) panel_height = 480U;
    const uint64_t minimum_margin = 8U;
    if (panel_width > width - minimum_margin * 2U)
        panel_width = width - minimum_margin * 2U;
    if (panel_height > height - top_bar - bottom_bar - minimum_margin * 2U)
        panel_height = height - top_bar - bottom_bar - minimum_margin * 2U;
    const uint64_t panel_x = (width - panel_width) / 2U;
    const uint64_t panel_y = top_bar + (height - top_bar - bottom_bar -
                              panel_height) / 2U;

    graphics_fill_rect(panel_x + 5U, panel_y + 6U, panel_width, panel_height,
                       2, 7, 19);
    graphics_fill_rect(panel_x, panel_y, panel_width, panel_height,
                       18, 31, 54);
    graphics_draw_rect(panel_x, panel_y, panel_width, panel_height,
                       56, 88, 119);
    graphics_fill_rect(panel_x, panel_y, panel_width, 38U, 24, 42, 69);
    graphics_draw_text(panel_x + 18U, panel_y + 15U, "NEXUS DESKTOP",
                       1, 225, 241, 255);
    graphics_fill_rect(panel_x + panel_width - 48U, panel_y + 14U,
                       8U, 8U, 231, 107, 119);

    const uint64_t title_y = panel_y + 76U;
    graphics_draw_text(panel_x + 32U, title_y, "WELCOME TO NEXUS OS",
                       2, 239, 249, 255);
    graphics_draw_text(panel_x + 34U, title_y + 28U,
                       "ORIGINAL KERNEL DESKTOP PREVIEW",
                       1, 108, 216, 229);

    const uint64_t tile_y = title_y + 74U;
    uint64_t tile_width = (panel_width - 80U) / 3U;
    if (tile_width > 190U) tile_width = 190U;
    const char *labels[3] = {"FILES", "BROWSER", "SETTINGS"};
    const uint8_t accent[3][3] = {
        {45, 177, 202}, {117, 136, 244}, {82, 198, 155}
    };

    for (uint32_t i = 0; i < 3U; ++i) {
        const uint64_t tile_x = panel_x + 28U +
                                (uint64_t)i * (tile_width + 10U);
        graphics_fill_rect(tile_x, tile_y, tile_width, 74U,
                           27, 45, 72);
        graphics_draw_rect(tile_x, tile_y, tile_width, 74U,
                           58, 87, 117);
        graphics_fill_rect(tile_x + 12U, tile_y + 12U, 22U, 22U,
                           accent[i][0], accent[i][1], accent[i][2]);
        graphics_draw_text(tile_x + 12U, tile_y + 47U, labels[i],
                           1, 233, 243, 252);
    }

    graphics_draw_text(panel_x + 30U, panel_y + panel_height - 42U,
                       "KERNEL RUNNING - INPUT AND APPS IN DEVELOPMENT",
                       1, 170, 191, 211);

    graphics_fill_rect(0, height - bottom_bar, width, bottom_bar,
                       10, 19, 37);
    graphics_fill_rect(0, height - bottom_bar, width, 2,
                       38, 75, 105);
    const uint64_t dock_y = height - bottom_bar + bottom_bar / 2U - 8U;
    for (uint32_t i = 0; i < 3U; ++i) {
        const uint64_t dock_x = width / 2U - 44U + (uint64_t)i * 28U;
        graphics_fill_rect(dock_x, dock_y, 16U, 16U,
                           accent[i][0], accent[i][1], accent[i][2]);
    }
    graphics_draw_text(18U, height - bottom_bar / 2U - 4U,
                       "NEXUS OS", 1, 178, 202, 221);

    serial_write("DESKTOP: framebuffer validated and desktop shell rendered\r\n");
    serial_write("NEXUS DESKTOP FRAMEBUFFER READY\r\n");
    return true;
}
