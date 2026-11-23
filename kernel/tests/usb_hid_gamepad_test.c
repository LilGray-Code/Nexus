#include "usb_hid_gamepad.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t standard_gamepad[] = {
    0x05,0x01, 0x09,0x05, 0xA1,0x01,
    0x15,0x00, 0x26,0xFF,0x00, 0x75,0x08, 0x95,0x02,
    0x09,0x30, 0x09,0x31, 0x81,0x02,
    0x05,0x09, 0x19,0x01, 0x29,0x08, 0x15,0x00,
    0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x05,0x01, 0x09,0x39, 0x15,0x00, 0x25,0x07,
    0x75,0x04, 0x95,0x01, 0x81,0x02,
    0x75,0x04, 0x95,0x01, 0x81,0x01,
    0xC0
};

static void test_standard_gamepad(void) {
    struct nexus_hid_gamepad_layout layout = {0};
    assert(nexus_hid_gamepad_parse_descriptor(
        standard_gamepad, sizeof(standard_gamepad), &layout));
    assert(layout.axis_count == 2U);
    assert(layout.button_count == 8U);
    assert(layout.has_hat);
    assert(layout.report_bytes == 4U);
    const uint8_t report[4] = {0x80U,0x7FU,0x05U,0x03U};
    struct nexus_hid_gamepad_state state = {0};
    assert(nexus_hid_gamepad_decode_report(&layout, report, sizeof(report),
                                           &state));
    assert(state.axes[0] == 128 && state.axes[1] == 127);
    assert(state.buttons == 5U);
    assert(state.hat == 3);
}

static void test_signed_axes_and_report_id(void) {
    const uint8_t descriptor[] = {
        0x05,0x01,0x09,0x04,0xA1,0x01,
        0x85,0x02, 0x15,0x80, 0x25,0x7F, 0x75,0x08, 0x95,0x01,
        0x09,0x30, 0x81,0x02, 0xC0
    };
    struct nexus_hid_gamepad_layout layout = {0};
    assert(nexus_hid_gamepad_parse_descriptor(
        descriptor, sizeof(descriptor), &layout));
    assert(layout.has_report_id && layout.report_id == 2U);
    const uint8_t report[] = {2U,0x80U};
    struct nexus_hid_gamepad_state state = {0};
    assert(nexus_hid_gamepad_decode_report(&layout, report, sizeof(report),
                                           &state));
    assert(state.axes[0] == -128);
    const uint8_t wrong_id[] = {3U,0U};
    assert(!nexus_hid_gamepad_decode_report(&layout, wrong_id,
                                            sizeof(wrong_id), &state));
}

static void test_rejects_invalid_gamepad_logical_ranges(void) {
    const uint8_t invalid_axis_range[] = {
        0x05,0x01, 0x09,0x05, 0xA1,0x01,
        0x15,0x0A, 0x25,0x01, 0x75,0x08, 0x95,0x01,
        0x09,0x30, 0x81,0x02, 0xC0
    };
    struct nexus_hid_gamepad_layout layout = {0};
    assert(!nexus_hid_gamepad_parse_descriptor(
        invalid_axis_range, sizeof(invalid_axis_range), &layout));

    const uint8_t invalid_hat_range[] = {
        0x05,0x01, 0x09,0x05, 0xA1,0x01,
        0x15,0x07, 0x25,0x01, 0x75,0x04, 0x95,0x01,
        0x09,0x39, 0x81,0x02, 0xC0
    };
    assert(!nexus_hid_gamepad_parse_descriptor(
        invalid_hat_range, sizeof(invalid_hat_range), &layout));
}

static void test_find_report_protocol_gamepad_interface(void) {
    const uint8_t configuration[] = {
        9,0x02, 34,0, 1,1,0,0x80,50,
        9,0x04, 2,0,1,3,0,0,0,
        9,0x21, 0x11,0x01,0,1,0x22,
        (uint8_t)sizeof(standard_gamepad),0,
        7,0x05, 0x81,3,64,0,4
    };
    struct nexus_hid_gamepad_interface interface = {0};
    assert(nexus_hid_gamepad_find_interface(
        configuration, sizeof(configuration), &interface));
    assert(interface.interface_number == 2U);
    assert(interface.configuration_value == 1U);
    assert(interface.endpoint_address == 0x81U);
    assert(interface.max_packet == 64U);
    assert(interface.interval == 4U);
    assert(interface.report_descriptor_length == sizeof(standard_gamepad));
}

static void test_reject_unsupported_hid_interface_endpoints(void) {
    const uint8_t valid[] = {
        9,0x02, 34,0, 1,1,0,0x80,50,
        9,0x04, 2,0,1,3,0,0,0,
        9,0x21, 0x11,0x01,0,1,0x22,
        (uint8_t)sizeof(standard_gamepad),0,
        7,0x05, 0x81,3,64,0,4
    };
    uint8_t configuration[sizeof(valid)];
    struct nexus_hid_gamepad_interface interface = {0};

    memcpy(configuration, valid, sizeof(valid));
    configuration[29] = 0x01U; /* OUT endpoint. */
    assert(!nexus_hid_gamepad_find_interface(
        configuration, sizeof(configuration), &interface));

    memcpy(configuration, valid, sizeof(valid));
    configuration[30] = 2U; /* Bulk rather than interrupt. */
    assert(!nexus_hid_gamepad_find_interface(
        configuration, sizeof(configuration), &interface));

    memcpy(configuration, valid, sizeof(valid));
    configuration[31] = 127U; /* Packet larger than supported HID reports. */
    assert(!nexus_hid_gamepad_find_interface(
        configuration, sizeof(configuration), &interface));

    memcpy(configuration, valid, sizeof(valid));
    assert(!nexus_hid_gamepad_find_interface(
        configuration, sizeof(configuration) - 1U, &interface));
}

static void test_reject_malformed_gamepad_hid_descriptors(void) {
    const uint8_t valid[] = {
        9,0x02, 34,0, 1,1,0,0x80,50,
        9,0x04, 2,0,1,3,0,0,0,
        9,0x21, 0x11,0x01,0,1,0x22,
        (uint8_t)sizeof(standard_gamepad),0,
        7,0x05, 0x81,3,64,0,4
    };
    uint8_t configuration[sizeof(valid)];
    struct nexus_hid_gamepad_interface interface = {0};

    memcpy(configuration, valid, sizeof(valid));
    configuration[18] = 8U; /* HID descriptor is truncated. */
    assert(!nexus_hid_gamepad_find_interface(
        configuration, sizeof(configuration), &interface));

    memcpy(configuration, valid, sizeof(valid));
    configuration[5] = 0U; /* Configuration value zero is invalid. */
    assert(!nexus_hid_gamepad_find_interface(
        configuration, sizeof(configuration), &interface));

    memcpy(configuration, valid, sizeof(valid));
    configuration[2] = 35U; /* wTotalLength exceeds the buffer. */
    assert(!nexus_hid_gamepad_find_interface(
        configuration, sizeof(configuration), &interface));
}

static void test_rejects_truncated_and_oversized_descriptors(void) {
    struct nexus_hid_gamepad_layout layout = {0};
    const uint8_t truncated[] = {0x05U};
    assert(!nexus_hid_gamepad_parse_descriptor(
        truncated, sizeof(truncated), &layout));
    uint8_t oversized[4097] = {0U};
    assert(!nexus_hid_gamepad_parse_descriptor(
        oversized, sizeof(oversized), &layout));
}

static void test_decode_rejects_bad_length_without_mutating_state(void) {
    struct nexus_hid_gamepad_layout layout = {0};
    assert(nexus_hid_gamepad_parse_descriptor(
        standard_gamepad, sizeof(standard_gamepad), &layout));
    struct nexus_hid_gamepad_state state;
    memset(&state, 0x5A, sizeof(state));
    const struct nexus_hid_gamepad_state before = state;
    const uint8_t short_report[2] = {0U,0U};
    assert(!nexus_hid_gamepad_decode_report(
        &layout, short_report, sizeof(short_report), &state));
    assert(memcmp(&state, &before, sizeof(state)) == 0);
}

int main(void) {
    test_standard_gamepad();
    test_signed_axes_and_report_id();
    test_find_report_protocol_gamepad_interface();
    test_reject_unsupported_hid_interface_endpoints();
    test_reject_malformed_gamepad_hid_descriptors();
    test_rejects_invalid_gamepad_logical_ranges();
    test_rejects_truncated_and_oversized_descriptors();
    test_decode_rejects_bad_length_without_mutating_state();
    puts("NEXUS USB HID GAMEPAD PARSER TESTS PASSED");
    return 0;
}
