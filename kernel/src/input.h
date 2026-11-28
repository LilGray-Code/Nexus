#ifndef NEXUS_INPUT_H
#define NEXUS_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "usb_hid.h"

/* 256 slots (255 usable) shared by PS/2 and USB keyboard/mouse producers. */
#define NEXUS_INPUT_EVENT_QUEUE_SIZE 256U

enum nexus_input_event_type {
    NEXUS_INPUT_KEYBOARD = 1,
    NEXUS_INPUT_MOUSE = 2
};

struct nexus_input_event {
    uint8_t type;
    /* Bit 8 marks an extended Set-1 key; bit 7 remains the release marker. */
    uint16_t scancode;
    uint32_t mouse_buttons;
    int16_t mouse_dx;
    int16_t mouse_dy;
    int8_t mouse_wheel;
};

bool input_init(void);
bool input_ps2_keyboard_ready(void);
bool input_ps2_mouse_ready(void);
void input_keyboard_interrupt(void);
void input_mouse_interrupt(void);
/* Bounded PS/2 controller drain used as a fallback when IRQ routing fails. */
size_t input_poll_ps2(void);
/* Called by the boot CPU while draining the single-producer input queue. */
bool input_pop_event(struct nexus_input_event *event);
/* Number of input events dropped because the shared ring was full. */
uint32_t input_dropped_event_count(void);
void input_usb_keyboard_report_state(struct nexus_usb_keyboard_state *state,
                                     const uint8_t report[8]);
void input_usb_keyboard_report(const uint8_t report[8]);
void input_usb_mouse_event(const struct nexus_usb_mouse_event *event);
void input_usb_mouse_report_sized(const uint8_t *report, size_t report_length);
void input_usb_mouse_report(const uint8_t report[3]);
/* Submit decoded gamepad state without changing keyboard/mouse event layout. */
void input_usb_gamepad_event(const struct nexus_usb_gamepad_event *event);
bool input_pop_gamepad_event(struct nexus_usb_gamepad_event *event);

#ifdef NEXUS_INPUT_HOST_TEST
/* Test-only entry point for the PS/2 byte-stream decoder. */
void input_test_keyboard_scancode(uint8_t scancode);
void input_test_mouse_device_id(uint8_t id);
void input_test_mouse_byte(uint8_t value);
void input_test_ps2_ready(bool keyboard, bool mouse);
void input_test_ps2_byte(uint8_t status, uint8_t value);
#endif

#endif
