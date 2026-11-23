#include "input.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

void serial_write(const char *text) { (void)text; }
void serial_write_hex(uint64_t value) { (void)value; }
void pic_irq_unmask(uint8_t irq) { (void)irq; }

static const uint8_t descriptor[] = {
    0x05,0x01, 0x09,0x05, 0xA1,0x01,
    0x15,0x00, 0x26,0xFF,0x00, 0x75,0x08, 0x95,0x02,
    0x09,0x30, 0x09,0x31, 0x81,0x02,
    0x05,0x09, 0x19,0x01, 0x29,0x08, 0x15,0x00,
    0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
    0xC0
};

static void test_gamepad_report_reaches_desktop_queue(void) {
    struct nexus_hid_gamepad_layout layout = {0};
    assert(nexus_hid_gamepad_parse_descriptor(
        descriptor, sizeof(descriptor), &layout));
    const uint8_t report[3] = {0x80U,0x7FU,0x05U};
    assert(input_usb_gamepad_report(&layout, report, sizeof(report)));
    struct nexus_input_event event = {0};
    assert(input_pop_event(&event));
    assert(event.type == NEXUS_INPUT_GAMEPAD);
    assert(event.gamepad_axis_count == 2U);
    assert(event.gamepad_axes[0] == 128 && event.gamepad_axes[1] == 127);
    assert(event.gamepad_buttons == 5U);
}

static void test_invalid_report_does_not_enqueue_event(void) {
    struct nexus_hid_gamepad_layout layout = {0};
    assert(nexus_hid_gamepad_parse_descriptor(
        descriptor, sizeof(descriptor), &layout));
    const uint8_t report[2] = {0U,0U};
    assert(!input_usb_gamepad_report(&layout, report, sizeof(report)));
    struct nexus_input_event event = {0};
    assert(!input_pop_event(&event));
}

int main(void) {
    test_gamepad_report_reaches_desktop_queue();
    test_invalid_report_does_not_enqueue_event();
    puts("NEXUS USB GAMEPAD INPUT QUEUE TESTS PASSED");
    return 0;
}
