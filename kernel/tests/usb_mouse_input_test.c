#include "input.h"
#include "serial.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

void serial_write(const char *text) { (void)text; }
void serial_write_hex(uint64_t value) { (void)value; }
void pic_irq_unmask(uint8_t irq) { (void)irq; }

static void test_usb_mouse_report_reaches_desktop_queue(void) {
    const uint8_t report[3] = {0x05U, 0xF4U, 0x07U};
    struct nexus_input_event event = {0};
    input_usb_mouse_report(report);
    assert(input_pop_event(&event));
    assert(event.type == NEXUS_INPUT_MOUSE);
    assert(event.mouse_buttons == 0x05U);
    assert(event.mouse_dx == -12);
    assert(event.mouse_dy == -7);
}

static void test_mouse_buttons_and_stationary_report(void) {
    const uint8_t report[3] = {0x02U, 0U, 0U};
    struct nexus_input_event event = {0};
    input_usb_mouse_report(report);
    assert(input_pop_event(&event));
    assert(event.type == NEXUS_INPUT_MOUSE);
    assert(event.mouse_buttons == 0x02U);
    assert(event.mouse_dx == 0);
    assert(event.mouse_dy == 0);
}

int main(void) {
    test_usb_mouse_report_reaches_desktop_queue();
    test_mouse_buttons_and_stationary_report();
    puts("NEXUS USB MOUSE INPUT QUEUE TESTS PASSED");
    return 0;
}
