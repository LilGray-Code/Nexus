#ifndef NEXUS_USB_HID_GAMEPAD_H
#define NEXUS_USB_HID_GAMEPAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_HID_GAMEPAD_MAX_AXES 8U
#define NEXUS_HID_GAMEPAD_MAX_BUTTONS 32U
#define NEXUS_HID_GAMEPAD_MAX_REPORT_BYTES 64U
#define NEXUS_HID_GAMEPAD_MAX_DESCRIPTOR_BYTES 4096U

struct nexus_hid_gamepad_interface {
    uint8_t interface_number;
    uint8_t configuration_value;
    uint8_t endpoint_address;
    uint8_t interval;
    uint16_t max_packet;
    uint16_t report_descriptor_length;
};

struct nexus_hid_gamepad_axis {
    uint16_t bit_offset;
    uint8_t bit_size;
    int32_t logical_min;
    int32_t logical_max;
    uint16_t usage;
};

struct nexus_hid_gamepad_layout {
    uint8_t report_id;
    uint8_t report_bytes;
    uint8_t axis_count;
    uint8_t button_count;
    bool has_report_id;
    bool has_hat;
    uint16_t hat_bit_offset;
    uint8_t hat_bit_size;
    int32_t hat_logical_min;
    int32_t hat_logical_max;
    struct nexus_hid_gamepad_axis axes[NEXUS_HID_GAMEPAD_MAX_AXES];
    uint16_t button_bit_offsets[NEXUS_HID_GAMEPAD_MAX_BUTTONS];
};

struct nexus_hid_gamepad_state {
    int32_t axes[NEXUS_HID_GAMEPAD_MAX_AXES];
    uint32_t buttons;
    int32_t hat;
};

/* Find a report-protocol HID interface with a supported interrupt-IN
 * endpoint. Parse its report descriptor separately before using reports. */
bool nexus_hid_gamepad_find_interface(
    const uint8_t *configuration, size_t length,
    struct nexus_hid_gamepad_interface *interface);

/* Parse one bounded HID game-controller report descriptor. Unsupported or
 * ambiguous layouts are rejected rather than guessed. */
bool nexus_hid_gamepad_parse_descriptor(
    const uint8_t *descriptor, size_t length,
    struct nexus_hid_gamepad_layout *layout);

/* Decode one input report matching a previously parsed layout. */
bool nexus_hid_gamepad_decode_report(
    const struct nexus_hid_gamepad_layout *layout,
    const uint8_t *report, size_t length,
    struct nexus_hid_gamepad_state *state);

#endif
