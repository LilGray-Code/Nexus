#include "usb_hid.h"

#include <assert.h>
#include <stdio.h>

static const uint8_t gamepad_descriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x05,       /* Usage (Game Pad) */
    0xA1, 0x01,       /* Collection (Application) */
    0xA1, 0x00,       /* Collection (Physical) */
    0x09, 0x30,       /* Usage (X) */
    0x09, 0x31,       /* Usage (Y) */
    0x15, 0x81,       /* Logical Minimum (-127) */
    0x25, 0x7F,       /* Logical Maximum (127) */
    0x75, 0x08,       /* Report Size (8) */
    0x95, 0x02,       /* Report Count (2) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0x05, 0x09,       /* Usage Page (Button) */
    0x19, 0x01,       /* Usage Minimum (1) */
    0x29, 0x08,       /* Usage Maximum (8) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x01,       /* Logical Maximum (1) */
    0x75, 0x01,       /* Report Size (1) */
    0x95, 0x08,       /* Report Count (8) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0xC0,             /* End Collection */
    0xC0              /* End Collection */
};

static void test_axes_and_buttons(void) {
    struct nexus_usb_gamepad_layout layout = {0};
    assert(usb_hid_gamepad_parse_descriptor(gamepad_descriptor,
        sizeof(gamepad_descriptor), &layout));
    assert(layout.axis_count == 2U);
    assert(layout.button_count == 8U);
    assert(layout.report_id == 0U);
    assert(layout.report_bytes == 3U);

    const uint8_t report[3] = {0x81U, 0x7FU, 0x05U};
    struct nexus_usb_gamepad_event event = {0};
    assert(usb_hid_gamepad_decode(&layout, report, sizeof(report),
        3U, 0x045EU, 0x028EU, &event));
    assert(event.device_id == 3U);
    assert(event.vendor_id == 0x045EU);
    assert(event.product_id == 0x028EU);
    assert(event.axis_count == 2U);
    assert(event.axes[0] == -32767);
    assert(event.axes[1] == 32767);
    assert(event.buttons == 0x05U);
    assert(event.hat == 0xFFU);
}

static void test_report_id_and_length_checks(void) {
    const uint8_t descriptor[] = {
        0x05,0x01,0x09,0x05,0xA1,0x01,
        0x85,0x01,0x15,0x00,0x26,0xFF,0x00,
        0x75,0x08,0x95,0x02,0x09,0x30,0x09,0x31,0x81,0x02,
        0x05,0x09,0x19,0x01,0x29,0x02,0x15,0x00,0x25,0x01,
        0x75,0x01,0x95,0x02,0x81,0x02,0xC0
    };
    struct nexus_usb_gamepad_layout layout = {0};
    assert(usb_hid_gamepad_parse_descriptor(descriptor,
        sizeof(descriptor), &layout));
    assert(layout.report_id == 1U);
    assert(layout.report_bytes == 4U);
    const uint8_t report[4] = {1U, 0U, 0xFFU, 2U};
    struct nexus_usb_gamepad_event event = {0};
    assert(usb_hid_gamepad_decode(&layout, report, sizeof(report),
        1U, 1U, 2U, &event));
    assert(event.axes[0] == -32767);
    assert(event.axes[1] == 32767);
    assert(event.buttons == 1U);
    assert(!usb_hid_gamepad_decode(&layout, report, 2U,
        1U, 1U, 2U, &event));
    const uint8_t wrong_id[4] = {2U, 0U, 0U, 0U};
    assert(!usb_hid_gamepad_decode(&layout, wrong_id, sizeof(wrong_id),
        1U, 1U, 2U, &event));
}

static void test_malformed_descriptor_is_rejected(void) {
    struct nexus_usb_gamepad_layout layout = {0};
    const uint8_t truncated[] = {0x05, 0x01, 0x09};
    assert(!usb_hid_gamepad_parse_descriptor(truncated,
        sizeof(truncated), &layout));
    assert(!usb_hid_gamepad_parse_descriptor(gamepad_descriptor,
        sizeof(gamepad_descriptor), NULL));
    assert(!usb_hid_gamepad_parse_descriptor(NULL,
        sizeof(gamepad_descriptor), &layout));
}

int main(void) {
    test_axes_and_buttons();
    test_report_id_and_length_checks();
    test_malformed_descriptor_is_rejected();
    puts("NEXUS HID GAMEPAD PARSER/DECODER TESTS PASSED");
    return 0;
}
