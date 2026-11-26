#include "graphics.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define WIDTH 8U
#define HEIGHT 6U
#define GUARD 0xA5A55A5AU

int main(void) {
    uint32_t guarded[WIDTH * HEIGHT + 2U];
    for (size_t i = 0U; i < sizeof(guarded) / sizeof(guarded[0]); ++i)
        guarded[i] = GUARD;

    struct nexus_graphics_surface surface = {
        .address = &guarded[1],
        .width = WIDTH,
        .height = HEIGHT,
        .pitch = WIDTH * sizeof(uint32_t),
        .bits_per_pixel = 32U,
        .red_mask_size = 8U, .red_mask_shift = 16U,
        .green_mask_size = 8U, .green_mask_shift = 8U,
        .blue_mask_size = 8U, .blue_mask_shift = 0U
    };

    assert(graphics_init(&surface));
    assert(graphics_width() == WIDTH);
    assert(graphics_height() == HEIGHT);

    struct nexus_graphics_damage damage;
    assert(!graphics_take_damage(&damage));
    graphics_clear(0U, 0U, 0U);
    assert(graphics_take_damage(&damage));
    assert(damage.x == 0U && damage.y == 0U &&
           damage.width == WIDTH && damage.height == HEIGHT);
    graphics_fill_rect(2U, 1U, 3U, 2U, 255U, 0U, 0U);
    assert(guarded[1U + 1U * WIDTH + 2U] == 0x00FF0000U);
    assert(guarded[1U + 2U * WIDTH + 4U] == 0x00FF0000U);
    assert(guarded[1U + 0U * WIDTH + 2U] == 0U);
    assert(graphics_take_damage(&damage));
    assert(damage.x == 2U && damage.y == 1U &&
           damage.width == 3U && damage.height == 2U);
    assert(!graphics_take_damage(&damage));

    /* Four-corner gradient interpolation reaches exact edge colors and
     * reports one bounded damage rectangle for the entire raster pass.
     */
    graphics_fill_gradient(0U, 0U, WIDTH, HEIGHT,
        255U, 0U, 0U, 0U, 255U, 0U,
        0U, 0U, 255U, 255U, 255U, 255U);
    assert(guarded[1U] == 0x00FF0000U);
    assert(guarded[1U + WIDTH - 1U] == 0x0000FF00U);
    assert(guarded[1U + (HEIGHT - 1U) * WIDTH] == 0x000000FFU);
    assert(guarded[1U + HEIGHT * WIDTH - 1U] == 0x00FFFFFFU);
    assert(graphics_take_damage(&damage));
    assert(damage.x == 0U && damage.y == 0U &&
           damage.width == WIDTH && damage.height == HEIGHT);
    const uint32_t gradient_center =
        guarded[1U + (HEIGHT / 2U) * WIDTH + WIDTH / 2U];
    assert(((gradient_center >> 16) & 0xFFU) >= 120U &&
           ((gradient_center >> 16) & 0xFFU) <= 140U);
    assert(((gradient_center >> 8) & 0xFFU) >= 135U &&
           ((gradient_center >> 8) & 0xFFU) <= 155U);
    assert((gradient_center & 0xFFU) >= 140U &&
           (gradient_center & 0xFFU) <= 165U);
    assert(!graphics_take_damage(&damage));

    /* A gradient beyond the lower-right edge is safely clipped. */
    graphics_fill_gradient(WIDTH - 2U, HEIGHT - 2U, 16U, 16U,
        0U, 0U, 0U, 255U, 0U, 0U,
        0U, 255U, 0U, 0U, 0U, 255U);
    assert(graphics_take_damage(&damage));
    assert(damage.x == WIDTH - 2U && damage.y == HEIGHT - 2U &&
           damage.width == 2U && damage.height == 2U);
    assert(guarded[0U] == GUARD);
    assert(guarded[WIDTH * HEIGHT + 1U] == GUARD);

    /* Oversized rectangles must clip without wrapping their far edges. */
    graphics_draw_rect(1U, 1U, UINT64_MAX, UINT64_MAX,
                       0U, 255U, 0U);
    assert(guarded[1U + 1U * WIDTH + 1U] == 0x0000FF00U);
    assert(guarded[1U + (HEIGHT - 1U) * WIDTH + (WIDTH - 1U)] ==
           0x0000FF00U);

    /* Alpha blending preserves transparent pixels and composites opaque ones. */
    graphics_clear(0U, 0U, 0U);
    graphics_blend_rect(0U, 0U, 1U, 1U, 255U, 0U, 0U, 0U);
    assert(guarded[1U] == 0U);
    graphics_blend_rect(0U, 0U, 1U, 1U, 255U, 0U, 0U, 255U);
    assert(guarded[1U] == 0x00FF0000U);
    graphics_blend_rect(0U, 0U, 1U, 1U, 0U, 0U, 255U, 128U);
    assert(((guarded[1U] >> 16) & 0xFFU) >= 126U &&
           ((guarded[1U] >> 16) & 0xFFU) <= 127U);
    assert(((guarded[1U]) & 0xFFU) >= 128U &&
           ((guarded[1U]) & 0xFFU) <= 129U);
    graphics_blend_rect(7U, 5U, UINT64_MAX, UINT64_MAX,
                        0U, 255U, 0U, 128U);

    /* Filled circles include their axis edges but not outside corner pixels. */
    graphics_clear(0U, 0U, 0U);
    graphics_fill_circle(4U, 3U, 2U, 255U, 0U, 0U);
    assert(guarded[1U + 3U * WIDTH + 4U] == 0x00FF0000U);
    assert(guarded[1U + 3U * WIDTH + 2U] == 0x00FF0000U);
    assert(guarded[1U + 1U * WIDTH + 4U] == 0x00FF0000U);
    assert(guarded[1U + 1U * WIDTH + 2U] == 0U);

    /* Circles clip at every surface edge and safely skip remote centers. */
    graphics_clear(0U, 0U, 0U);
    graphics_fill_circle(0U, 0U, 2U, 0U, 255U, 0U);
    assert(guarded[1U] == 0x0000FF00U);
    assert(guarded[1U + 2U * WIDTH + 2U] == 0U);
    graphics_fill_circle(WIDTH + 3U, 2U, 2U, 255U, 255U, 255U);
    graphics_fill_circle(3U, HEIGHT + 2U, 1U, 255U, 255U, 255U);
    graphics_fill_circle(4U, 3U, 2049U, 255U, 255U, 255U);
    assert(guarded[1U + 3U * WIDTH + 4U] == 0U);

    /* Alpha circles blend their clipped spans using the same channel rules. */
    graphics_clear(0U, 0U, 0U);
    graphics_blend_circle(4U, 3U, 1U, 255U, 0U, 0U, 128U);
    assert(((guarded[1U + 3U * WIDTH + 4U] >> 16) & 0xFFU) >= 127U);
    assert(((guarded[1U + 3U * WIDTH + 4U] >> 16) & 0xFFU) <= 128U);
    graphics_blend_circle(4U, 3U, 1U, 0U, 255U, 0U, 0U);
    assert(guarded[1U + 3U * WIDTH + 4U] & 0x00FF0000U);

    /* Rounded rectangles clip safely and leave their corner pixels untouched. */
    graphics_clear(0U, 0U, 0U);
    graphics_round_rect(1U, 1U, 6U, 4U, 2U, 0U, 255U, 0U);
    const uint32_t rounded_top_left = guarded[1U + 1U * WIDTH + 1U];
    const uint32_t rounded_bottom_right =
        guarded[1U + 4U * WIDTH + 6U];
    assert((rounded_top_left & 0xFFU) == 0U);
    assert(((rounded_top_left >> 8) & 0xFFU) >= 63U &&
           ((rounded_top_left >> 8) & 0xFFU) <= 64U);
    assert(guarded[1U + 2U * WIDTH + 3U] == 0x0000FF00U);
    assert((rounded_bottom_right & 0xFFU) == 0U);
    assert(((rounded_bottom_right >> 8) & 0xFFU) >= 63U &&
           ((rounded_bottom_right >> 8) & 0xFFU) <= 64U);
    graphics_blend_round_rect(0U, 0U, UINT64_MAX, UINT64_MAX,
                              UINT32_MAX, 0U, 0U, 255U, 128U);
    assert(guarded[1U + 2U * WIDTH + 3U] != 0x0000FF00U);

    /* Fractional glyph scaling blends edge pixels instead of producing
     * hard one-pixel stair steps. The first A cell starts at x=1.5 pixels.
     */
    graphics_clear(0U, 0U, 0U);
    graphics_draw_text_aa(5U, 0U, "A", 98304U, 255U, 0U, 0U);
    const uint32_t antialiased_edge = guarded[1U + 6U];
    assert(((antialiased_edge >> 16) & 0xFFU) >= 126U &&
           ((antialiased_edge >> 16) & 0xFFU) <= 128U);
    assert(guarded[1U + 7U] == 0x00FF0000U);

    /* The desktop's 4/3 scale must also blend fractional glyph edges. */
    graphics_clear(0U, 0U, 0U);
    graphics_draw_text_aa(5U, 0U, "A", 87381U, 255U, 0U, 0U);
    const uint32_t desktop_scale_edge = guarded[1U + 6U];
    assert(((desktop_scale_edge >> 16) & 0xFFU) >= 168U &&
           ((desktop_scale_edge >> 16) & 0xFFU) <= 172U);

    /* Extreme text scale and origin must remain clipped to the surface. */
    graphics_draw_text(0U, 0U, "NEXUS", UINT32_MAX, 255U, 255U, 255U);
    graphics_draw_text_aa(0U, 0U, "NEXUS", UINT32_MAX,
                          255U, 255U, 255U);
    graphics_draw_text(UINT64_MAX - 2U, UINT64_MAX - 2U, "X",
                       UINT32_MAX, 255U, 255U, 255U);

    assert(guarded[0U] == GUARD);
    assert(guarded[WIDTH * HEIGHT + 1U] == GUARD);

    /* Reject misaligned pitches and overlapping channel masks. */
    surface.pitch = WIDTH * sizeof(uint32_t) + 1U;
    assert(!graphics_init(&surface));
    assert(graphics_width() == 0U);

    surface.pitch = WIDTH * sizeof(uint32_t);
    surface.green_mask_shift = 16U;
    assert(!graphics_init(&surface));
    assert(graphics_height() == 0U);

    return 0;
}
