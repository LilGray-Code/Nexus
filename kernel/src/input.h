#ifndef NEXUS_INPUT_H
#define NEXUS_INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "usb_hid_gamepad.h"

enum nexus_input_event_type {
    NEXUS_INPUT_KEYBOARD = 1,
    NEXUS_INPUT_MOUSE = 2,
    NEXUS_INPUT_GAMEPAD = 3
};

struct nexus_input_event {
    uint8_t type;
    uint8_t scancode;
    uint8_t mouse_buttons;
    int16_t mouse_dx;
    int16_t mouse_dy;
    uint8_t gamepad_axis_count;
    int32_t gamepad_axes[NEXUS_HID_GAMEPAD_MAX_AXES];
    uint32_t gamepad_buttons;
    int32_t gamepad_hat;
};

bool input_init(void);
void input_keyboard_interrupt(void);
void input_mouse_interrupt(void);
/* Called by the boot CPU with interrupts disabled. */
bool input_pop_event(struct nexus_input_event *event);
/* Submit an eight-byte USB HID boot-keyboard report to the desktop queue. */
void input_usb_keyboard_report(const uint8_t report[8]);
/* Submit a three-byte USB HID boot-mouse report to the desktop queue. */
void input_usb_mouse_report(const uint8_t report[3]);
bool input_usb_gamepad_report(
    const struct nexus_hid_gamepad_layout *layout,
    const uint8_t *report, size_t length);

#endif
