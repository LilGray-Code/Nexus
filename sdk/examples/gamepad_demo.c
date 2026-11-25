#include "nexus/app.h"

#include <stdbool.h>

#define FRAME_WIDTH 256U
#define FRAME_HEIGHT 192U

static uint32_t frame[FRAME_WIDTH * FRAME_HEIGHT];

static void fill(uint32_t color) {
    for (size_t i = 0U; i < FRAME_WIDTH * FRAME_HEIGHT; ++i)
        frame[i] = color;
}

static void rectangle(uint32_t x, uint32_t y, uint32_t width,
                      uint32_t height, uint32_t color) {
    if (x >= FRAME_WIDTH || y >= FRAME_HEIGHT) return;
    if (width > FRAME_WIDTH - x) width = FRAME_WIDTH - x;
    if (height > FRAME_HEIGHT - y) height = FRAME_HEIGHT - y;
    for (uint32_t row = 0U; row < height; ++row)
        for (uint32_t column = 0U; column < width; ++column)
            frame[(y + row) * FRAME_WIDTH + x + column] = color;
}

static uint32_t axis_position(int16_t value) {
    return (uint32_t)(((int32_t)value + 32767) * 100 / 65534);
}

static void draw_state(const struct nexus_app_gamepad_event *gamepad,
                       bool connected) {
    fill(0x00101828U);
    rectangle(12U, 12U, 232U, 20U, connected ? 0x002D8C72U : 0x00404A5AU);

    for (uint32_t axis = 0U; axis < 4U; ++axis) {
        const uint32_t y = 52U + axis * 28U;
        rectangle(18U, y, 220U, 14U, 0x00313D50U);
        rectangle(18U + 110U, y, 2U, 14U, 0x006D7C91U);
        if (connected && axis < gamepad->axis_count) {
            const uint32_t pos = axis_position(gamepad->axes[axis]);
            if (pos < 50U)
                rectangle(18U + pos * 2U, y, 110U - pos * 2U, 14U,
                          0x002BB5D6U);
            else
                rectangle(128U, y, (pos - 50U) * 2U, 14U, 0x002BB5D6U);
        }
    }

    for (uint32_t button = 0U; button < 16U; ++button) {
        const uint32_t x = 20U + (button % 8U) * 28U;
        const uint32_t y = 174U + (button / 8U) * 12U;
        const bool pressed = connected &&
            (gamepad->buttons & (1U << button)) != 0U;
        rectangle(x, y, 18U, 7U,
                  pressed ? 0x00F2C14EU : 0x00505D70U);
    }

    if (connected && gamepad->hat != NEXUS_APP_GAMEPAD_HAT_NEUTRAL) {
        const uint32_t hat_x = 221U;
        const uint32_t hat_y = 24U;
        rectangle(hat_x, hat_y, 18U, 18U, 0x00D7E2EFU);
        rectangle(hat_x + (gamepad->hat % 3U) * 4U,
                  hat_y + (gamepad->hat / 3U) * 4U,
                  6U, 6U, 0x00E09F3EU);
    }
}

void _start(void) {
    if (nexus_app_abi_version() != NEXUS_APP_ABI_VERSION)
        nexus_app_exit(2U);

    struct nexus_app_display_info display = {0};
    if (nexus_app_get_display(&display) != 0 ||
        display.width < FRAME_WIDTH || display.height < FRAME_HEIGHT ||
        display.pixel_format != NEXUS_APP_PIXEL_XRGB8888)
        nexus_app_exit(3U);

    struct nexus_app_gamepad_event gamepad = {0};
    bool connected = false;
    const uint32_t origin_x = (uint32_t)(display.width - FRAME_WIDTH) / 2U;
    const uint32_t origin_y = (uint32_t)(display.height - FRAME_HEIGHT) / 2U;

    for (;;) {
        struct nexus_app_input_event input;
        while (nexus_app_input_poll(&input) > 0) {
            if (input.type == NEXUS_APP_INPUT_KEYBOARD &&
                (input.scancode & 0x7FU) == 0x01U &&
                (input.scancode & NEXUS_APP_KEY_RELEASE) == 0U)
                nexus_app_exit(0U);
        }
        struct nexus_app_gamepad_event next;
        while (nexus_app_gamepad_poll(&next) > 0) {
            gamepad = next;
            connected = (next.flags & NEXUS_APP_GAMEPAD_DISCONNECTED) == 0U;
        }
        draw_state(&gamepad, connected);
        if (nexus_app_blit(origin_x, origin_y, FRAME_WIDTH, FRAME_HEIGHT,
                           frame, FRAME_WIDTH) != 0)
            nexus_app_exit(4U);
        (void)nexus_app_sleep_ticks(1U);
    }
}
