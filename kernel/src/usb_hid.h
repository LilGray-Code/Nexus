#ifndef NEXUS_USB_HID_H
#define NEXUS_USB_HID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct nexus_usb_mouse_event {
    uint8_t buttons;
    int16_t dx;
    int16_t dy;
    int8_t wheel;
};

struct nexus_usb_keyboard_state {
    uint8_t previous_report[8];
    bool have_previous_report;
};

bool usb_hid_mouse_decode_report(const uint8_t *report, size_t report_length,
                                 struct nexus_usb_mouse_event *event);
bool usb_hid_mouse_decode(const uint8_t report[3],
                          struct nexus_usb_mouse_event *event);

void usb_hid_keyboard_state_reset(struct nexus_usb_keyboard_state *state);
size_t usb_hid_keyboard_decode_state(struct nexus_usb_keyboard_state *state,
                                     const uint8_t report[8],
                                     uint16_t *scancodes,
                                     size_t capacity);
void usb_hid_keyboard_reset(void);
size_t usb_hid_keyboard_decode(const uint8_t report[8],
                               uint16_t *scancodes,
                               size_t capacity);

#define NEXUS_USB_GAMEPAD_MAX_AXES 8U
#define NEXUS_USB_GAMEPAD_MAX_BUTTONS 32U
#define NEXUS_USB_GAMEPAD_MAX_REPORT_BYTES 64U
#define NEXUS_USB_GAMEPAD_EVENT_DISCONNECTED 0x0001U

struct nexus_usb_gamepad_axis {
    uint16_t bit_offset;
    uint8_t bit_size;
    uint8_t usage;
    int32_t logical_minimum;
    int32_t logical_maximum;
};

struct nexus_usb_gamepad_layout {
    uint8_t report_id;
    uint8_t report_bytes;
    uint8_t axis_count;
    uint8_t button_count;
    uint16_t button_offsets[NEXUS_USB_GAMEPAD_MAX_BUTTONS];
    uint8_t button_sizes[NEXUS_USB_GAMEPAD_MAX_BUTTONS];
    struct nexus_usb_gamepad_axis axes[NEXUS_USB_GAMEPAD_MAX_AXES];
    uint16_t hat_offset;
    uint8_t hat_size;
    int32_t hat_minimum;
    int32_t hat_maximum;
};

struct nexus_usb_gamepad_event {
    uint16_t device_id;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t flags;
    uint32_t buttons;
    uint8_t axis_count;
    uint8_t hat;
    uint8_t reserved2[2];
    int16_t axes[NEXUS_USB_GAMEPAD_MAX_AXES];
};

/* Parse a bounded HID report descriptor; only one input report ID is accepted. */
bool usb_hid_gamepad_parse_descriptor(
    const uint8_t *descriptor, size_t length,
    struct nexus_usb_gamepad_layout *layout);

/* Decode a report into normalized axes, a 32-bit button mask and an 8-way hat. */
bool usb_hid_gamepad_decode(
    const struct nexus_usb_gamepad_layout *layout,
    const uint8_t *report, size_t report_length,
    uint16_t device_id, uint16_t vendor_id, uint16_t product_id,
    struct nexus_usb_gamepad_event *event);

#endif
