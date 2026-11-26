#include "input.h"
#include "serial.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

void serial_write(const char *text) { (void)text; }
void serial_write_hex(uint64_t value) { (void)value; }
void pic_irq_unmask(uint8_t irq) { (void)irq; }

static void expect_key(uint16_t expected) {
    struct nexus_input_event event = {0};
    assert(input_pop_event(&event));
    assert(event.type == NEXUS_INPUT_KEYBOARD);
    assert(event.scancode == expected);
}

static void test_extended_keys_and_right_modifiers(void) {
    input_test_keyboard_scancode(0xE0U);
    input_test_keyboard_scancode(0x48U); /* Up arrow */
    expect_key(0x0148U);

    input_test_keyboard_scancode(0xE0U);
    input_test_keyboard_scancode(0x1DU); /* Right Ctrl down */
    expect_key(0x011DU);
    input_test_keyboard_scancode(0xE0U);
    input_test_keyboard_scancode(0x9DU); /* Right Ctrl up */
    expect_key(0x019DU);

    input_test_keyboard_scancode(0xE0U);
    input_test_keyboard_scancode(0x38U); /* Right Alt / AltGr down */
    expect_key(0x0138U);
    input_test_keyboard_scancode(0xE0U);
    input_test_keyboard_scancode(0xB8U);
    expect_key(0x01B8U);
}

static void test_print_screen_fake_shift_and_pause_sequence(void) {
    struct nexus_input_event event = {0};
    input_test_keyboard_scancode(0xE0U);
    input_test_keyboard_scancode(0x2AU); /* Fake Shift make: discard */
    input_test_keyboard_scancode(0xE0U);
    input_test_keyboard_scancode(0x37U); /* Print Screen make */
    expect_key(0x0137U);
    input_test_keyboard_scancode(0xE0U);
    input_test_keyboard_scancode(0xB7U); /* Print Screen break */
    expect_key(0x01B7U);
    input_test_keyboard_scancode(0xE0U);
    input_test_keyboard_scancode(0xAAU); /* Fake Shift break: discard */

    input_test_keyboard_scancode(0xE1U);
    input_test_keyboard_scancode(0x1DU);
    input_test_keyboard_scancode(0x45U);
    input_test_keyboard_scancode(0xE1U);
    input_test_keyboard_scancode(0x9DU);
    input_test_keyboard_scancode(0xC5U);
    assert(!input_pop_event(&event)); /* Pause's five following bytes ignored. */
}

static void test_intellimouse_wheel_and_explorer_buttons(void) {
    struct nexus_input_event event = {0};
    input_test_mouse_device_id(3U);
    input_test_mouse_byte(0x09U); /* Sync + left button */
    input_test_mouse_byte(0xFEU);
    input_test_mouse_byte(0x04U);
    input_test_mouse_byte(0x0FU); /* Wheel -1 */
    assert(input_pop_event(&event));
    assert(event.type == NEXUS_INPUT_MOUSE);
    assert(event.mouse_buttons == 0x01U);
    assert(event.mouse_dx == -2);
    assert(event.mouse_dy == -4);
    assert(event.mouse_wheel == -1);

    input_test_mouse_device_id(4U);
    input_test_mouse_byte(0x08U);
    input_test_mouse_byte(0U);
    input_test_mouse_byte(0U);
    input_test_mouse_byte(0x10U); /* Explorer side button 4 */
    assert(input_pop_event(&event));
    assert(event.mouse_buttons == 0x08U);
    assert(event.mouse_wheel == 0);
}

static void test_shared_input_queue_burst_and_recovery(void) {
    struct nexus_input_event event = {0};
    assert(input_dropped_event_count() == 0U);

    /* A 256-slot ring deliberately keeps one slot empty to distinguish full. */
    for (uint32_t i = 0U; i < NEXUS_INPUT_EVENT_QUEUE_SIZE - 1U; ++i)
        input_test_keyboard_scancode(0x1EU);
    assert(input_dropped_event_count() == 0U);

    /* A full queue drops only the newest event and records the loss. */
    input_test_keyboard_scancode(0x30U);
    assert(input_dropped_event_count() == 1U);

    size_t delivered = 0U;
    while (input_pop_event(&event)) {
        assert(event.type == NEXUS_INPUT_KEYBOARD);
        assert(event.scancode == 0x1EU);
        ++delivered;
    }
    assert(delivered == NEXUS_INPUT_EVENT_QUEUE_SIZE - 1U);
    assert(input_dropped_event_count() == 1U);

    /* Once drained, the same ring accepts new events again. */
    input_test_keyboard_scancode(0x2EU);
    expect_key(0x2EU);
    assert(input_dropped_event_count() == 1U);
}

int main(void) {
    test_extended_keys_and_right_modifiers();
    test_print_screen_fake_shift_and_pause_sequence();
    test_intellimouse_wheel_and_explorer_buttons();
    test_shared_input_queue_burst_and_recovery();
    puts("NEXUS PS/2 EXTENDED KEYBOARD AND MOUSE TESTS PASSED");
    return 0;
}
