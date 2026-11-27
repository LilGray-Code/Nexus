#ifndef NEXUS_USB_HID_GENERIC_H
#define NEXUS_USB_HID_GENERIC_H

#include "usb_hid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum nexus_usb_hid_generic_kind {
    NEXUS_USB_HID_GENERIC_NONE = 0,
    NEXUS_USB_HID_GENERIC_KEYBOARD = 1,
    NEXUS_USB_HID_GENERIC_MOUSE = 2
};

struct nexus_usb_hid_generic_layout {
    enum nexus_usb_hid_generic_kind kind;
    bool has_report_id;
    uint8_t report_id;
    uint8_t input_bytes;

    uint16_t modifier_bit_offset[8];
    uint8_t modifier_mask;

    uint16_t key_array_bit_offset;
    uint8_t key_array_size;
    uint8_t key_array_count;
    uint8_t key_usage_minimum;
    uint8_t key_usage_maximum;

    uint16_t button_bit_offset[8];
    uint8_t button_count;

    uint16_t x_bit_offset;
    uint16_t y_bit_offset;
    uint16_t wheel_bit_offset;
    uint8_t x_size;
    uint8_t y_size;
    uint8_t wheel_size;
    int32_t x_logical_minimum;
    int32_t y_logical_minimum;
    int32_t wheel_logical_minimum;
    bool x_relative;
    bool y_relative;
    bool wheel_relative;
};

/* Parse a bounded HID report descriptor containing one keyboard or mouse
 * input report. Multiple report IDs in one interface are deliberately
 * rejected; callers may still use the boot-protocol interface when present. */
bool usb_hid_generic_parse_descriptor(
    const uint8_t *descriptor, size_t descriptor_length,
    struct nexus_usb_hid_generic_layout *layout);

bool usb_hid_generic_decode_keyboard(
    const struct nexus_usb_hid_generic_layout *layout,
    const uint8_t *report, size_t report_length, uint8_t boot_report[8]);

bool usb_hid_generic_decode_mouse(
    const struct nexus_usb_hid_generic_layout *layout,
    const uint8_t *report, size_t report_length,
    struct nexus_usb_mouse_event *event);

#endif
