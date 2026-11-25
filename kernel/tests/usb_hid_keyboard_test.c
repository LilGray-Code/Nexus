#include "usb_hid.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static void test_basic_keys_and_deduplication(void) {
    uint8_t report[8] = {0U,0U,4U,5U,40U,44U,0U,0U};
    uint16_t output[8] = {0U};
    usb_hid_keyboard_reset();
    size_t n = usb_hid_keyboard_decode(report, output, (sizeof(output) / sizeof(output[0])));
    assert(n == 4U);
    assert(output[0] == 0x1EU); /* A */
    assert(output[1] == 0x30U); /* B */
    assert(output[2] == 0x1CU); /* Enter */
    assert(output[3] == 0x39U); /* Space */

    n = usb_hid_keyboard_decode(report, output, (sizeof(output) / sizeof(output[0])));
    assert(n == 0U);
}

static void test_release_then_press(void) {
    uint8_t report[8] = {0U};
    uint16_t output[8] = {0U};
    usb_hid_keyboard_reset();
    report[2] = 4U;
    assert(usb_hid_keyboard_decode(report, output, 8U) == 1U);
    assert(output[0] == 0x1EU);
    report[2] = 0U;
    assert(usb_hid_keyboard_decode(report, output, 8U) == 1U);
    assert(output[0] == 0x9EU); /* A release */
    report[2] = 4U;
    assert(usb_hid_keyboard_decode(report, output, 8U) == 1U);
    assert(output[0] == 0x1EU);
}

static void test_shift_and_caps_lock_transitions(void) {
    uint8_t report[8] = {0U};
    uint16_t output[14] = {0U};
    usb_hid_keyboard_reset();

    report[0] = 0x02U; /* Left Shift down */
    assert(usb_hid_keyboard_decode(report, output, 14U) == 1U);
    assert(output[0] == 0x2AU);

    report[2] = 4U; /* A down while Shift is held */
    assert(usb_hid_keyboard_decode(report, output, 14U) == 1U);
    assert(output[0] == 0x1EU);

    report[0] = 0U; /* Left Shift up */
    assert(usb_hid_keyboard_decode(report, output, 14U) == 1U);
    assert(output[0] == 0xAAU);

    report[2] = 0U; /* A up */
    assert(usb_hid_keyboard_decode(report, output, 14U) == 1U);
    assert(output[0] == 0x9EU);

    report[0] = 0x20U; /* Right Shift down/up */
    assert(usb_hid_keyboard_decode(report, output, 14U) == 1U);
    assert(output[0] == 0x36U);
    report[0] = 0U;
    assert(usb_hid_keyboard_decode(report, output, 14U) == 1U);
    assert(output[0] == 0xB6U);

    report[2] = 57U; /* Caps Lock down/up */
    assert(usb_hid_keyboard_decode(report, output, 14U) == 1U);
    assert(output[0] == 0x3AU);
    report[2] = 0U;
    assert(usb_hid_keyboard_decode(report, output, 14U) == 1U);
    assert(output[0] == 0xBAU);
}

static void test_full_report_transition_capacity(void) {
    uint8_t previous[8] = {0U,0U,4U,5U,6U,7U,8U,9U};
    uint8_t current[8] = {0x22U,0U,10U,11U,12U,13U,14U,15U};
    uint16_t output[14] = {0U};
    usb_hid_keyboard_reset();
    assert(usb_hid_keyboard_decode(previous, output, 14U) == 6U);
    const size_t count = usb_hid_keyboard_decode(current, output, 14U);
    assert(count == 14U); /* two shifts, six releases, six new makes */
    assert(output[0] == 0x2AU && output[1] == 0x36U);
    for (size_t i = 2U; i < 8U; ++i)
        assert((output[i] & 0x80U) != 0U);
    for (size_t i = 8U; i < 14U; ++i)
        assert((output[i] & 0x80U) == 0U);
}

static void test_arrows_and_function_keys(void) {
    uint8_t report[8] = {0U,0U,82U,81U,79U,80U,58U,0U};
    uint8_t expected[5] = {0x48U,0x50U,0x4DU,0x4BU,0x3BU};
    uint16_t output[8] = {0U};
    usb_hid_keyboard_reset();
    size_t n = usb_hid_keyboard_decode(report, output, 8U);
    assert(n == 5U);
    for (size_t i = 0U; i < n; ++i) assert(output[i] == expected[i]);
}

static void test_rollover_does_not_poison_state(void) {
    uint8_t report[8] = {0U,0U,4U,0U,0U,0U,0U,0U};
    uint16_t output[8] = {0U};
    usb_hid_keyboard_reset();
    assert(usb_hid_keyboard_decode(report, output, 8U) == 1U);
    report[2] = 1U; /* ErrorRollOver */
    assert(usb_hid_keyboard_decode(report, output, 8U) == 0U);
    report[2] = 4U;
    assert(usb_hid_keyboard_decode(report, output, 8U) == 0U);
}

static void test_capacity_and_null_arguments(void) {
    uint8_t report[8] = {0U,0U,4U,5U,6U,7U,8U,9U};
    uint16_t output[2] = {0U};
    usb_hid_keyboard_reset();
    assert(usb_hid_keyboard_decode(report, output, 2U) == 2U);
    assert(usb_hid_keyboard_decode(NULL, output, 2U) == 0U);
    assert(usb_hid_keyboard_decode(report, NULL, 1U) == 0U);
}

static void test_all_modifier_transitions_and_full_capacity(void) {
    uint8_t previous[8] = {0U,0U,4U,5U,6U,7U,8U,9U};
    uint8_t current[8] = {0xFFU,0U,10U,11U,12U,13U,14U,15U};
    uint16_t output[24] = {0U};
    static const uint16_t expected_modifiers[8] = {
        0x01DU, 0x02AU, 0x038U, 0x15BU,
        0x11DU, 0x036U, 0x138U, 0x15CU
    };
    usb_hid_keyboard_reset();
    assert(usb_hid_keyboard_decode(previous, output, (sizeof(output) / sizeof(output[0]))) == 6U);

    /* Eight modifiers plus six releases and six new presses = 20 events. */
    size_t count = usb_hid_keyboard_decode(current, output, (sizeof(output) / sizeof(output[0])));
    assert(count == 20U);
    for (size_t i = 0U; i < 8U; ++i)
        assert(output[i] == expected_modifiers[i]);

    static const uint8_t expected_releases[6] = {
        0x9EU, 0xB0U, 0xAEU, 0xA0U, 0x92U, 0xA1U
    };
    for (size_t i = 0U; i < 6U; ++i)
        assert(output[8U + i] == expected_releases[i]);

    static const uint8_t expected_presses[6] = {
        0x22U, 0x23U, 0x17U, 0x24U, 0x25U, 0x26U
    };
    for (size_t i = 0U; i < 6U; ++i)
        assert(output[14U + i] == expected_presses[i]);

    current[0] = 0U; /* Release Ctrl, Shift, Alt and both GUI keys. */
    for (size_t i = 2U; i < 8U; ++i) current[i] = 0U;
    count = usb_hid_keyboard_decode(current, output, (sizeof(output) / sizeof(output[0])));
    assert(count == 14U);
    static const uint16_t expected_modifier_releases[8] = {
        0x09DU, 0x0AAU, 0x0B8U, 0x1DBU,
        0x19DU, 0x0B6U, 0x1B8U, 0x1DCU
    };
    for (size_t i = 0U; i < 8U; ++i)
        assert(output[i] == expected_modifier_releases[i]);
}

static void test_extended_navigation_keypad_and_right_modifiers(void) {
    uint8_t report[8] = {0U};
    uint16_t output[8] = {0U};
    usb_hid_keyboard_reset();

    report[0] = 0x10U; /* Right Ctrl uses the E0 Set-1 namespace. */
    assert(usb_hid_keyboard_decode(report, output, 8U) == 1U);
    assert(output[0] == 0x11DU);
    report[0] = 0U;
    assert(usb_hid_keyboard_decode(report, output, 8U) == 1U);
    assert(output[0] == 0x19DU);

    report[2] = 79U; /* Right arrow */
    assert(usb_hid_keyboard_decode(report, output, 8U) == 1U);
    assert(output[0] == 0x14DU);
    report[2] = 0U;
    assert(usb_hid_keyboard_decode(report, output, 8U) == 1U);
    assert(output[0] == 0x1CDU);

    report[2] = 89U; /* Keypad 1 */
    assert(usb_hid_keyboard_decode(report, output, 8U) == 1U);
    assert(output[0] == 0x4FU);
    report[2] = 0U;
    (void)usb_hid_keyboard_decode(report, output, 8U);

    report[0] = 0x40U; /* Right Alt / AltGr */
    assert(usb_hid_keyboard_decode(report, output, 8U) == 1U);
    assert(output[0] == 0x138U);
    report[0] = 0U;
    assert(usb_hid_keyboard_decode(report, output, 8U) == 1U);
    assert(output[0] == 0x1B8U);
}

int main(void) {
    test_basic_keys_and_deduplication();
    test_release_then_press();
    test_shift_and_caps_lock_transitions();
    test_full_report_transition_capacity();
    test_all_modifier_transitions_and_full_capacity();
    test_extended_navigation_keypad_and_right_modifiers();
    test_arrows_and_function_keys();
    test_rollover_does_not_poison_state();
    test_capacity_and_null_arguments();
    puts("NEXUS USB HID KEYBOARD DECODER TESTS PASSED");
    return 0;
}
