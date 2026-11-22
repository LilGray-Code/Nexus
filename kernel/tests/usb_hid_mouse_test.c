#include "usb_hid.h"

#include <assert.h>
#include <stdio.h>

static void test_signed_motion_and_button_mask(void) {
    const uint8_t report[3] = {0xFDU, 0xFEU, 0x03U};
    struct nexus_usb_mouse_event event = {0};
    assert(usb_hid_mouse_decode(report, &event));
    assert(event.buttons == 0x05U);
    assert(event.dx == -2);
    assert(event.dy == -3);
}

static void test_positive_motion(void) {
    const uint8_t report[3] = {0x02U, 12U, 7U};
    struct nexus_usb_mouse_event event = {0};
    assert(usb_hid_mouse_decode(report, &event));
    assert(event.buttons == 0x02U);
    assert(event.dx == 12);
    assert(event.dy == -7);
}

static void test_negative_extreme_is_safe(void) {
    const uint8_t report[3] = {0U, 0x80U, 0x80U};
    struct nexus_usb_mouse_event event = {0};
    assert(usb_hid_mouse_decode(report, &event));
    assert(event.dx == -128);
    assert(event.dy == 128);
}

static void test_invalid_arguments(void) {
    struct nexus_usb_mouse_event event = {0};
    const uint8_t report[3] = {0U, 0U, 0U};
    assert(!usb_hid_mouse_decode(NULL, &event));
    assert(!usb_hid_mouse_decode(report, NULL));
}

int main(void) {
    test_signed_motion_and_button_mask();
    test_positive_motion();
    test_negative_extreme_is_safe();
    test_invalid_arguments();
    puts("NEXUS USB HID MOUSE DECODER TESTS PASSED");
    return 0;
}
