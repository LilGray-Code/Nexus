#ifndef NEXUS_USB_HID_H
#define NEXUS_USB_HID_H

#include <stddef.h>
#include <stdint.h>

/*
 * Translate a USB HID boot-keyboard report into the legacy Set-1 make codes
 * consumed by the existing desktop input layer. Only newly pressed keys are
 * returned. Rollover/error reports are ignored without changing key state.
 */
void usb_hid_keyboard_reset(void);
size_t usb_hid_keyboard_decode(const uint8_t report[8],
                               uint8_t *scancodes,
                               size_t capacity);

#endif
