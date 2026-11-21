#ifndef NEXUS_KEYBOARD_H
#define NEXUS_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

/* Legacy i8042/PS/2 set-1 keyboard input; returns translated ASCII keys. */
void keyboard_init(void);
bool keyboard_poll(char *character);

#endif
