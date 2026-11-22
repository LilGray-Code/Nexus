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
    report[2] = 0U;
    assert(usb_hid_keyboard_decode(report, output, 8U) == 0U);
    report[2] = 4U;
    assert(usb_hid_keyboard_decode(report, output, 8U) == 1U);
    assert(output[0] == 0x1EU);
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
    test_arrows_and_function_keys();
    test_rollover_does_not_poison_state();
    test_capacity_and_null_arguments();
    puts("NEXUS USB HID KEYBOARD DECODER TESTS PASSED");
    return 0;
}
