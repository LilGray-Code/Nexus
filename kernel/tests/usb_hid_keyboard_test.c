#include "usb_hid.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static void test_basic_keys_and_deduplication(void) {
    uint8_t report[8] = {0U,0U,4U,5U,40U,44U,0U,0U};
    uint8_t output[8] = {0U};
    usb_hid_keyboard_reset();
    size_t n = usb_hid_keyboard_decode(report, output, sizeof(output));
    assert(n == 4U);
    assert(output[0] == 0x1EU); /* A */
    assert(output[1] == 0x30U); /* B */
    assert(output[2] == 0x1CU); /* Enter */
    assert(output[3] == 0x39U); /* Space */

    n = usb_hid_keyboard_decode(report, output, sizeof(output));
    assert(n == 0U);
}

static void test_release_then_press(void) {
    uint8_t report[8] = {0U};
    uint8_t output[8] = {0U};
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
    uint8_t output[14] = {0U};
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
    uint8_t output[14] = {0U};
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
    uint8_t output[8] = {0U};
    usb_hid_keyboard_reset();
    size_t n = usb_hid_keyboard_decode(report, output, 8U);
    assert(n == 5U);
    for (size_t i = 0U; i < n; ++i) assert(output[i] == expected[i]);
}

static void test_rollover_does_not_poison_state(void) {
    uint8_t report[8] = {0U,0U,4U,0U,0U,0U,0U,0U};
    uint8_t output[8] = {0U};
    usb_hid_keyboard_reset();
    assert(usb_hid_keyboard_decode(report, output, 8U) == 1U);
    report[2] = 1U; /* ErrorRollOver */
    assert(usb_hid_keyboard_decode(report, output, 8U) == 0U);
    report[2] = 4U;
    assert(usb_hid_keyboard_decode(report, output, 8U) == 0U);
}

static void test_capacity_and_null_arguments(void) {
    uint8_t report[8] = {0U,0U,4U,5U,6U,7U,8U,9U};
    uint8_t output[2] = {0U};
    usb_hid_keyboard_reset();
    assert(usb_hid_keyboard_decode(report, output, 2U) == 2U);
    assert(usb_hid_keyboard_decode(NULL, output, 2U) == 0U);
    assert(usb_hid_keyboard_decode(report, NULL, 1U) == 0U);
}

int main(void) {
    test_basic_keys_and_deduplication();
    test_release_then_press();
    test_shift_and_caps_lock_transitions();
    test_full_report_transition_capacity();
    test_arrows_and_function_keys();
    test_rollover_does_not_poison_state();
    test_capacity_and_null_arguments();
    puts("NEXUS USB HID KEYBOARD DECODER TESTS PASSED");
    return 0;
}
