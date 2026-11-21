#ifndef NEXUS_DESKTOP_H
#define NEXUS_DESKTOP_H

#include <stdbool.h>
#include <limine.h>

#include "input.h"

/* Draws the in-kernel framebuffer desktop and handles basic app navigation. */
bool desktop_run_process_selftest(void);
bool desktop_show(const struct limine_framebuffer *framebuffer);
void desktop_handle_input(const struct nexus_input_event *event);

#endif
