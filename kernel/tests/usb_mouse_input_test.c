#include "input.h"
#include "serial.h"
#include "usb_hid.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

void serial_write(const char *text) { (void)text; }
void serial_write_hex(uint64_t value) { (void)value; }
void pic_irq_unmask(uint8_t irq) { (void)irq; }

static void test_usb_mouse_report_reaches_desktop_queue(void) {
    const uint8_t report[3] = {0x05U, 0xF4U, 0x07U};
    struct nexus_input_event event = {0};
    input_usb_mouse_report(report);
    assert(input_pop_event(&event));
    assert(event.type == NEXUS_INPUT_MOUSE);
    assert(event.mouse_buttons == 0x05U);
    assert(event.mouse_dx == -12);
    assert(event.mouse_dy == -7);
}

static void test_mouse_buttons_and_stationary_report(void) {
    const uint8_t report[3] = {0x02U, 0U, 0U};
    struct nexus_input_event event = {0};
    input_usb_mouse_report(report);
    assert(input_pop_event(&event));
    assert(event.type == NEXUS_INPUT_MOUSE);
    assert(event.mouse_buttons == 0x02U);
    assert(event.mouse_dx == 0);
    assert(event.mouse_dy == 0);
}

static void test_usb_mouse_wheel_reaches_input_queue(void) {
    const uint8_t report[4] = {0U,0U,0U,0xFFU};
    struct nexus_input_event event = {0};
    input_usb_mouse_report_sized(report, sizeof(report));
    assert(input_pop_event(&event));
    assert(event.type == NEXUS_INPUT_MOUSE && event.mouse_wheel == -1);
}

static void test_decoded_high_resolution_mouse_event_reaches_input_queue(void) {
    const struct nexus_usb_mouse_event decoded = {
        .buttons = 0x80008109U,
        .dx = 300,
        .dy = -300,
        .wheel = 512,
        .horizontal_wheel = -256
    };
    struct nexus_input_event event = {0};
    input_usb_mouse_event(&decoded);
    assert(input_pop_event(&event));
    assert(event.type == NEXUS_INPUT_MOUSE);
    assert(event.mouse_buttons == 0x80008109U);
    assert(event.mouse_dx == 300);
    assert(event.mouse_dy == -300);
    assert(event.mouse_wheel == 512);
    assert(event.mouse_horizontal_wheel == -256);
}

static void test_usb_keyboard_report_preserves_right_control(void) {
    const uint8_t down[8] = {0x10U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t up[8] = {0U};
    struct nexus_input_event event = {0};

    usb_hid_keyboard_reset();
    input_usb_keyboard_report(down);
    assert(input_pop_event(&event));
    assert(event.type == NEXUS_INPUT_KEYBOARD);
    assert(event.scancode == 0x11DU);

    input_usb_keyboard_report(up);
    assert(input_pop_event(&event));
    assert(event.scancode == 0x19DU);
}

int main(void) {
    test_usb_mouse_report_reaches_desktop_queue();
    test_mouse_buttons_and_stationary_report();
    test_usb_mouse_wheel_reaches_input_queue();
    test_decoded_high_resolution_mouse_event_reaches_input_queue();
    test_usb_keyboard_report_preserves_right_control();
    puts("NEXUS USB MOUSE INPUT QUEUE TESTS PASSED");
    return 0;
}
