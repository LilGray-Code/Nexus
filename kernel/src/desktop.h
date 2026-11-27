#ifndef NEXUS_DESKTOP_H
#define NEXUS_DESKTOP_H

#include <stdbool.h>
#include <limine.h>

#include "input.h"

/* Draws the in-kernel framebuffer desktop and handles basic app navigation. */
enum nexus_desktop_process_test_result {
    NEXUS_DESKTOP_PROCESS_TEST_FAILED = -1,
    NEXUS_DESKTOP_PROCESS_TEST_SKIPPED = 0,
    NEXUS_DESKTOP_PROCESS_TEST_PASSED = 1
};
enum nexus_desktop_process_test_result desktop_run_process_selftest(void);
/* Selects the isolated installer session before the framebuffer shell starts. */
void desktop_set_installer_mode(bool enabled);
bool desktop_show(const struct limine_framebuffer *framebuffer);
void desktop_handle_input(const struct nexus_input_event *event);
/* Capture the active framebuffer to SCREEN.PPM in the current writable NXFS folder. */
bool desktop_capture_screenshot(void);

#endif
