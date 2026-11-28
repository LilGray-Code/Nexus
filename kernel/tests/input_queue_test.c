#include "input.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

void serial_write(const char *text) { (void)text; }
void serial_write_hex(uint64_t value) { (void)value; }

static void submit_mouse(uint64_t buttons, int16_t dx, int16_t dy,
                         int16_t wheel, int16_t hwheel) {
    const struct nexus_usb_mouse_event event = {
        .buttons = buttons,
        .dx = dx,
        .dy = dy,
        .wheel = wheel,
        .horizontal_wheel = hwheel
    };
    input_usb_mouse_event(&event);
}

static struct nexus_input_event pop_required(void) {
    struct nexus_input_event event = {0};
    assert(input_pop_event(&event));
    return event;
}

static void test_adjacent_motion_is_coalesced(void) {
    submit_mouse(0U, 1, -2, 1, 0);
    submit_mouse(0U, 2, 3, -1, 0);
    submit_mouse(0U, -4, 5, 2, 0);

    const struct nexus_input_event event = pop_required();
    assert(event.type == NEXUS_INPUT_MOUSE);
    assert(event.mouse_buttons == 0U);
    assert(event.mouse_dx == -1);
    assert(event.mouse_dy == 6);
    assert(event.mouse_wheel == 2);
    assert(!input_pop_event(&(struct nexus_input_event){0}));
}

static void test_button_edges_are_never_coalesced(void) {
    submit_mouse(0U, 5, 0, 0, 0);
    submit_mouse(1U, 0, 0, 0, 0);
    submit_mouse(0U, 0, 0, 0, 0);

    const struct nexus_input_event move = pop_required();
    const struct nexus_input_event press = pop_required();
    const struct nexus_input_event release = pop_required();
    assert(move.type == NEXUS_INPUT_MOUSE && move.mouse_dx == 5);
    assert(press.type == NEXUS_INPUT_MOUSE && press.mouse_buttons == 1U);
    assert(release.type == NEXUS_INPUT_MOUSE && release.mouse_buttons == 0U);
    assert(!input_pop_event(&(struct nexus_input_event){0}));
}

static void test_motion_saturates_without_integer_wrap(void) {
    submit_mouse(0U, 32760, -32760, 32760, 32760);
    submit_mouse(0U, 100, -100, 100, 100);
    const struct nexus_input_event event = pop_required();
    assert(event.mouse_dx == INT16_MAX);
    assert(event.mouse_dy == INT16_MIN);
    assert(event.mouse_wheel == INT16_MAX);
    assert(event.mouse_hwheel == INT16_MAX);
    assert(!input_pop_event(&(struct nexus_input_event){0}));
}


static void test_high_resolution_horizontal_wheel_coalesces(void) {
    submit_mouse(0U, 0, 0, 0, 300);
    submit_mouse(0U, 0, 0, 0, -100);
    const struct nexus_input_event event = pop_required();
    assert(event.mouse_hwheel == 200);
    assert(event.mouse_wheel == 0);
    assert(!input_pop_event(&(struct nexus_input_event){0}));
}

static void test_full_button_mask_is_preserved(void) {
    submit_mouse(0x8000000000000000ULL, 0, 0, 0, 0);
    const struct nexus_input_event event = pop_required();
    assert(event.mouse_buttons == 0x8000000000000000ULL);
    assert(!input_pop_event(&(struct nexus_input_event){0}));
}

static void test_keyboard_event_separates_mouse_motion(void) {
    submit_mouse(0U, 1, 0, 0, 0);
    input_test_keyboard_scancode(0x1EU);
    submit_mouse(0U, 2, 0, 0, 0);

    const struct nexus_input_event first = pop_required();
    const struct nexus_input_event key = pop_required();
    const struct nexus_input_event second = pop_required();
    assert(first.type == NEXUS_INPUT_MOUSE && first.mouse_dx == 1);
    assert(key.type == NEXUS_INPUT_KEYBOARD && key.scancode == 0x1EU);
    assert(second.type == NEXUS_INPUT_MOUSE && second.mouse_dx == 2);
    assert(!input_pop_event(&(struct nexus_input_event){0}));
}

int main(void) {
    test_adjacent_motion_is_coalesced();
    test_button_edges_are_never_coalesced();
    test_motion_saturates_without_integer_wrap();
    test_high_resolution_horizontal_wheel_coalesces();
    test_full_button_mask_is_preserved();
    test_keyboard_event_separates_mouse_motion();
    puts("NEXUS INPUT MOUSE-QUEUE REGRESSION TESTS PASSED");
    return 0;
}
