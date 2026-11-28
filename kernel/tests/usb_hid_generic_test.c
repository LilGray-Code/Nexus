#include "usb_hid_generic.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static const uint8_t generic_keyboard_descriptor[] = {
    0x05,0x01, 0x09,0x06, 0xA1,0x01,
    0x05,0x07, 0x19,0xE0, 0x29,0xE7,
    0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x75,0x08, 0x95,0x01, 0x81,0x01,
    0x19,0x00, 0x29,0x65, 0x15,0x00, 0x25,0x65,
    0x75,0x08, 0x95,0x06, 0x81,0x00,
    0xC0
};

static const uint8_t generic_keyboard_report_id_descriptor[] = {
    0x05,0x01, 0x09,0x06, 0xA1,0x01,
    0x85,0x01,
    0x05,0x07, 0x19,0xE0, 0x29,0xE7,
    0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x75,0x08, 0x95,0x01, 0x81,0x01,
    0x19,0x00, 0x29,0x65, 0x15,0x00, 0x25,0x65,
    0x75,0x08, 0x95,0x06, 0x81,0x00,
    0xC0
};

static const uint8_t generic_mouse_descriptor[] = {
    0x05,0x01, 0x09,0x02, 0xA1,0x01,
    0x09,0x01, 0xA1,0x00,
    0x05,0x09, 0x19,0x01, 0x29,0x03,
    0x15,0x00, 0x25,0x01, 0x95,0x03, 0x75,0x01, 0x81,0x02,
    0x95,0x01, 0x75,0x05, 0x81,0x01,
    0x05,0x01, 0x09,0x30, 0x09,0x31,
    0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x02, 0x81,0x06,
    0x09,0x38, 0x75,0x08, 0x95,0x01, 0x81,0x06,
    0xC0, 0xC0
};

static const uint8_t generic_mouse_16bit_descriptor[] = {
    0x05,0x01, 0x09,0x02, 0xA1,0x01,
    0x09,0x01, 0xA1,0x00,
    0x05,0x09, 0x19,0x01, 0x29,0x05,
    0x15,0x00, 0x25,0x01, 0x95,0x05, 0x75,0x01, 0x81,0x02,
    0x95,0x01, 0x75,0x03, 0x81,0x01,
    0x05,0x01, 0x09,0x30, 0x09,0x31,
    0x16,0x00,0x80, 0x26,0xFF,0x7F,
    0x75,0x10, 0x95,0x02, 0x81,0x06,
    0x09,0x38, 0x15,0x81, 0x25,0x7F,
    0x75,0x08, 0x95,0x01, 0x81,0x06,
    0xC0, 0xC0
};

static void test_generic_keyboard(void) {
    struct nexus_usb_hid_generic_layout layout = {0};
    assert(usb_hid_generic_parse_descriptor(generic_keyboard_descriptor,
        sizeof(generic_keyboard_descriptor), &layout));
    assert(layout.kind == NEXUS_USB_HID_GENERIC_KEYBOARD);
    assert(!layout.has_report_id);
    assert(layout.input_bytes == 8U);
    assert(layout.modifier_mask == 0xFFU);
    assert(layout.key_array_count == 6U);
    assert(layout.key_array_size == 8U);

    const uint8_t report[8] = {0x05U,0U,0x04U,0x05U,0U,0U,0U,0U};
    uint8_t boot_report[8] = {0};
    assert(usb_hid_generic_decode_keyboard(&layout, report,
        sizeof(report), boot_report));
    assert(boot_report[0] == 0x05U);
    assert(boot_report[2] == 0x04U);
    assert(boot_report[3] == 0x05U);

    const uint8_t short_report[7] = {0};
    assert(!usb_hid_generic_decode_keyboard(&layout, short_report,
        sizeof(short_report), boot_report));
}

static void test_generic_keyboard_with_report_id(void) {
    struct nexus_usb_hid_generic_layout layout = {0};
    assert(usb_hid_generic_parse_descriptor(
        generic_keyboard_report_id_descriptor,
        sizeof(generic_keyboard_report_id_descriptor), &layout));
    assert(layout.kind == NEXUS_USB_HID_GENERIC_KEYBOARD);
    assert(layout.has_report_id);
    assert(layout.report_id == 1U);
    assert(layout.input_bytes == 8U);

    const uint8_t report[9] = {1U,0x02U,0U,0x04U,0U,0U,0U,0U,0U};
    uint8_t boot_report[8] = {0};
    assert(usb_hid_generic_decode_keyboard(&layout, report,
        sizeof(report), boot_report));
    assert(boot_report[0] == 0x02U);
    assert(boot_report[2] == 0x04U);

    const uint8_t wrong_id[9] = {2U,0U,0U,0U,0U,0U,0U,0U,0U};
    assert(!usb_hid_generic_decode_keyboard(&layout, wrong_id,
        sizeof(wrong_id), boot_report));
}

static void test_generic_mouse(void) {
    struct nexus_usb_hid_generic_layout layout = {0};
    assert(usb_hid_generic_parse_descriptor(generic_mouse_descriptor,
        sizeof(generic_mouse_descriptor), &layout));
    assert(layout.kind == NEXUS_USB_HID_GENERIC_MOUSE);
    assert(layout.input_bytes == 4U);
    assert(layout.button_count == 3U);
    assert(layout.x_size == 8U && layout.y_size == 8U);
    assert(layout.wheel_size == 8U);
    assert(layout.x_relative && layout.y_relative && layout.wheel_relative);

    const uint8_t report[4] = {0x05U,0xFEU,0x03U,0xFFU};
    struct nexus_usb_mouse_event event = {0};
    assert(usb_hid_generic_decode_mouse(&layout, report,
        sizeof(report), &event));
    assert(event.buttons == 0x05U);
    assert(event.dx == -2);
    assert(event.dy == -3);
    assert(event.wheel == -1);

    const uint8_t short_report[3] = {0};
    assert(!usb_hid_generic_decode_mouse(&layout, short_report,
        sizeof(short_report), &event));
}

static void test_high_resolution_mouse_motion_is_not_clipped(void) {
    struct nexus_usb_hid_generic_layout layout = {0};
    assert(usb_hid_generic_parse_descriptor(
        generic_mouse_16bit_descriptor,
        sizeof(generic_mouse_16bit_descriptor), &layout));
    assert(layout.kind == NEXUS_USB_HID_GENERIC_MOUSE);
    assert(layout.input_bytes == 6U);
    assert(layout.x_size == 16U && layout.y_size == 16U);

    /* Five buttons, padding, X=300, Y=-300 and wheel=-1. */
    const uint8_t report[6] = {0x05U,0x2CU,0x01U,0xD4U,0xFEU,0xFFU};
    struct nexus_usb_mouse_event event = {0};
    assert(usb_hid_generic_decode_mouse(&layout, report,
        sizeof(report), &event));
    assert(event.buttons == 0x05U);
    assert(event.dx == 300);
    assert(event.dy == 300);
    assert(event.wheel == -1);

    /* Saturate safely at the signed 16-bit bounds without wraparound. */
    const uint8_t extremes[6] = {
        0x01U, 0x00U, 0x80U, 0x00U, 0x80U, 0x7FU
    };
    assert(usb_hid_generic_decode_mouse(&layout, extremes,
        sizeof(extremes), &event));
    assert(event.dx == INT16_MIN);
    assert(event.dy == INT16_MAX);
    assert(event.wheel == 127);
}


static const uint8_t generic_mouse_32button_descriptor[] = {
    0x05,0x01, 0x09,0x02, 0xA1,0x01,
    0x05,0x09, 0x19,0x01, 0x29,0x20,
    0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x20, 0x81,0x02,
    0x05,0x01, 0x09,0x30, 0x09,0x31,
    0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x02, 0x81,0x06,
    0xC0
};

static void test_generic_mouse_32_buttons(void) {
    struct nexus_usb_hid_generic_layout layout = {0};
    assert(usb_hid_generic_parse_descriptor(
        generic_mouse_32button_descriptor,
        sizeof(generic_mouse_32button_descriptor), &layout));
    assert(layout.button_count == 32U);
    const uint8_t report[6] = {0x80U,0x80U,0x80U,0x80U,1U,2U};
    struct nexus_usb_mouse_event event = {0};
    assert(usb_hid_generic_decode_mouse(&layout, report,
        sizeof(report), &event));
    assert(event.buttons == 0x80808080U);
    assert(event.dx == 1 && event.dy == -2);
}


static const uint8_t generic_mouse_hwheel_descriptor[] = {
    0x05,0x01, 0x09,0x02, 0xA1,0x01,
    0x09,0x01, 0xA1,0x00,
    0x05,0x09, 0x19,0x01, 0x29,0x03,
    0x15,0x00, 0x25,0x01, 0x95,0x03, 0x75,0x01, 0x81,0x02,
    0x95,0x01, 0x75,0x05, 0x81,0x01,
    0x05,0x01, 0x09,0x30, 0x09,0x31,
    0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x02, 0x81,0x06,
    0x09,0x38,
    0x16,0x00,0x80, 0x26,0xFF,0x7F, 0x75,0x10, 0x95,0x01, 0x81,0x06,
    0x05,0x0C, 0x0B,0x38,0x02,0x0C,0x00,
    0x16,0x00,0x80, 0x26,0xFF,0x7F, 0x75,0x10, 0x95,0x01, 0x81,0x06,
    0xC0,0xC0
};

static void test_generic_mouse_high_resolution_horizontal_scroll(void) {
    struct nexus_usb_hid_generic_layout layout = {0};
    assert(usb_hid_generic_parse_descriptor(
        generic_mouse_hwheel_descriptor,
        sizeof(generic_mouse_hwheel_descriptor), &layout));
    assert(layout.wheel_size == 16U);
    assert(layout.horizontal_wheel_size == 16U);
    assert(layout.horizontal_wheel_relative);
    const uint8_t report[7] = {
        0x05U, 0x01U, 0x02U, 0x00U, 0x01U, 0x00U, 0xFEU
    };
    struct nexus_usb_mouse_event event = {0};
    assert(usb_hid_generic_decode_mouse(&layout, report,
        sizeof(report), &event));
    assert(event.wheel == 256);
    assert(event.horizontal_wheel == -512);
}

static void test_descriptor_rejection_and_bounds(void) {
    struct nexus_usb_hid_generic_layout layout = {0};
    assert(!usb_hid_generic_parse_descriptor(NULL, 0U, &layout));
    assert(!usb_hid_generic_parse_descriptor(generic_keyboard_descriptor,
        sizeof(generic_keyboard_descriptor) - 1U, &layout));

    const uint8_t truncated_item[] = {0x05U};
    assert(!usb_hid_generic_parse_descriptor(truncated_item,
        sizeof(truncated_item), &layout));

    const uint8_t unrelated_consumer[] = {
        0x05,0x0C, 0x09,0x01, 0xA1,0x01,
        0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x01,
        0x09,0xE9, 0x81,0x02, 0xC0
    };
    assert(!usb_hid_generic_parse_descriptor(unrelated_consumer,
        sizeof(unrelated_consumer), &layout));
}

int main(void) {
    test_generic_keyboard();
    test_generic_keyboard_with_report_id();
    test_generic_mouse();
    test_high_resolution_mouse_motion_is_not_clipped();
    test_generic_mouse_32_buttons();
    test_generic_mouse_high_resolution_horizontal_scroll();
    test_descriptor_rejection_and_bounds();
    puts("NEXUS GENERIC USB HID KEYBOARD AND MOUSE TESTS PASSED");
    return 0;
}
