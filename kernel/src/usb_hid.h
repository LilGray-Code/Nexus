#ifndef NEXUS_USB_HID_H
#define NEXUS_USB_HID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Translate a USB HID boot-keyboard report into Set-1 make/break codes
 * consumed by the desktop input layer, including left/right Shift changes.
 * Rollover/error reports are ignored without changing key state.
 */
struct nexus_usb_mouse_event {
    uint8_t buttons;
    int16_t dx;
    int16_t dy;
};

/* Decode a three-byte USB HID boot-mouse report (buttons, X, Y). */
bool usb_hid_mouse_decode(const uint8_t report[3],
                          struct nexus_usb_mouse_event *event);

void usb_hid_keyboard_reset(void);
size_t usb_hid_keyboard_decode(const uint8_t report[8],
                               uint8_t *scancodes,
                               size_t capacity);

#endif
