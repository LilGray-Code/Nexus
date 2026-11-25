#ifndef NEXUS_INPUT_H
#define NEXUS_INPUT_H

#include <stdbool.h>
#include <stdint.h>

enum nexus_input_event_type {
    NEXUS_INPUT_KEYBOARD = 1,
    NEXUS_INPUT_MOUSE = 2
};

struct nexus_input_event {
    uint8_t type;
    /* Bit 8 marks an extended Set-1 key; bit 7 remains the release marker. */
    uint16_t scancode;
    uint8_t mouse_buttons;
    int16_t mouse_dx;
    int16_t mouse_dy;
    int8_t mouse_wheel;
};

bool input_init(void);
void input_keyboard_interrupt(void);
void input_mouse_interrupt(void);
/* Called by the boot CPU while draining the single-producer input queue. */
bool input_pop_event(struct nexus_input_event *event);
/* Submit a standard eight-byte USB HID boot-keyboard report. */
void input_usb_keyboard_report(const uint8_t report[8]);
/* Submit a standard three-byte USB HID boot-mouse report. */
void input_usb_mouse_report(const uint8_t report[3]);

#ifdef NEXUS_INPUT_HOST_TEST
/* Test-only entry point for the PS/2 byte-stream decoder. */
void input_test_keyboard_scancode(uint8_t scancode);
void input_test_mouse_device_id(uint8_t id);
void input_test_mouse_byte(uint8_t value);
#endif

#endif
