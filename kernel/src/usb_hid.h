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

/* Decode a standard three-byte USB HID boot-mouse report (no wheel field). */
bool usb_hid_mouse_decode(const uint8_t report[3],
                          struct nexus_usb_mouse_event *event);

void usb_hid_keyboard_reset(void);
/* Capacity is measured in uint16_t output entries, not bytes. */
size_t usb_hid_keyboard_decode(const uint8_t report[8],
                               uint16_t *scancodes,
                               size_t capacity);

#endif
