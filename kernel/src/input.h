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
    uint8_t scancode;
    uint8_t mouse_buttons;
    int16_t mouse_dx;
    int16_t mouse_dy;
};

bool input_init(void);
void input_keyboard_interrupt(void);
void input_mouse_interrupt(void);
/* Called by the boot CPU with interrupts disabled. */
bool input_pop_event(struct nexus_input_event *event);

#endif
