#include "nexus/app.h"

#define FRAME_WIDTH 160U
#define FRAME_HEIGHT 120U

static uint32_t frame[FRAME_WIDTH * FRAME_HEIGHT];

static void draw_frame(uint32_t ball_x, uint32_t ball_y) {
    for (uint32_t y = 0U; y < FRAME_HEIGHT; ++y) {
        for (uint32_t x = 0U; x < FRAME_WIDTH; ++x) {
            const uint32_t shade = 18U + (y * 28U / FRAME_HEIGHT);
            frame[y * FRAME_WIDTH + x] =
                (shade << 16) | ((shade + 8U) << 8) | (shade + 16U);
        }
    }

    for (int32_t y = -6; y <= 6; ++y) {
        for (int32_t x = -6; x <= 6; ++x) {
            if (x * x + y * y <= 36) {
                const uint32_t px = (uint32_t)((int32_t)ball_x + x);
                const uint32_t py = (uint32_t)((int32_t)ball_y + y);
                frame[py * FRAME_WIDTH + px] = 0x00F2C14EU;
            }
        }
    }
}

void _start(void) {
    if (nexus_app_abi_version() != NEXUS_APP_ABI_VERSION)
        nexus_app_exit(2U);

    struct nexus_app_display_info display = {0};
    if (nexus_app_get_display(&display) != 0 ||
        display.width < FRAME_WIDTH || display.height < FRAME_HEIGHT ||
        display.pixel_format != NEXUS_APP_PIXEL_XRGB8888) {
        nexus_app_exit(3U);
    }

    uint32_t x = FRAME_WIDTH / 2U;
    uint32_t y = FRAME_HEIGHT / 2U;
    int32_t vx = 2;
    int32_t vy = 1;
    const uint32_t origin_x = (uint32_t)(display.width - FRAME_WIDTH) / 2U;
    const uint32_t origin_y = (uint32_t)(display.height - FRAME_HEIGHT) / 2U;

    for (;;) {
        struct nexus_app_input_event event;
        while (nexus_app_input_poll(&event) > 0) {
            if (event.type == NEXUS_APP_INPUT_KEYBOARD &&
                (event.scancode & 0x7FU) == 0x01U &&
                (event.scancode & NEXUS_APP_KEY_RELEASE) == 0U) {
                nexus_app_exit(0U);
            }
        }

        x = (uint32_t)((int32_t)x + vx);
        y = (uint32_t)((int32_t)y + vy);
        if (x <= 7U || x >= FRAME_WIDTH - 8U) vx = -vx;
        if (y <= 7U || y >= FRAME_HEIGHT - 8U) vy = -vy;

        draw_frame(x, y);
        if (nexus_app_blit(origin_x, origin_y, FRAME_WIDTH, FRAME_HEIGHT,
                           frame, FRAME_WIDTH) != 0) {
            nexus_app_exit(4U);
        }
        (void)nexus_app_sleep_ticks(1U);
    }
}
