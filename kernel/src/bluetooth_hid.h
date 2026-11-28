#ifndef NEXUS_BLUETOOTH_HID_H
#define NEXUS_BLUETOOTH_HID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "usb_hid_generic.h"

#define NEXUS_BT_L2CAP_CID_ATT 0x0004U
#define NEXUS_BT_HIDP_TRANSACTION_DATA 0x0AU
#define NEXUS_BT_HIDP_REPORT_INPUT 0x01U
#define NEXUS_BT_ATT_HANDLE_VALUE_NOTIFICATION 0x1BU
#define NEXUS_BT_ATT_HANDLE_VALUE_INDICATION 0x1DU
#define NEXUS_BT_ATT_READ_RESPONSE 0x0BU
#define NEXUS_BT_HOGP_REPORT_TYPE_INPUT 0x01U
#define NEXUS_BT_HID_MAX_REPORT_BYTES 64U

struct nexus_bt_gatt_hid_service;

struct nexus_bt_l2cap_view {
    uint16_t channel_id;
    const uint8_t *payload;
    uint16_t payload_length;
};

struct nexus_bt_hid_report_view {
    uint16_t attribute_handle;
    const uint8_t *report;
    uint16_t report_length;
};

/* Per-peer HID decoder state, configured from the remote HID Report Map. */
struct nexus_bt_hid_input_device {
    bool configured;
    struct nexus_usb_hid_generic_multi_layout layout;
    struct nexus_usb_keyboard_state keyboard_states[
        NEXUS_USB_HID_GENERIC_MAX_REPORTS];
};

bool nexus_bt_l2cap_parse(const uint8_t *packet, size_t packet_length,
                          struct nexus_bt_l2cap_view *l2cap);
bool nexus_bt_hidp_parse_input(const uint8_t *packet, size_t packet_length,
                               struct nexus_bt_hid_report_view *report);
bool nexus_bt_hogp_parse_notification(
    const struct nexus_bt_l2cap_view *l2cap,
    struct nexus_bt_hid_report_view *report);
bool nexus_bt_hogp_parse_read_response(
    const uint8_t *att_pdu, size_t pdu_length, uint16_t report_handle,
    struct nexus_bt_hid_report_view *report);

/* Called after HOGP Report Map and Report Reference discovery has completed. */
bool nexus_bt_hid_input_configure(
    struct nexus_bt_hid_input_device *device,
    const uint8_t *report_map, size_t report_map_length);
bool nexus_bt_hid_input_dispatch_hogp_report(
    struct nexus_bt_hid_input_device *device, uint8_t report_id,
    uint8_t report_type, const uint8_t *report, size_t report_length);
bool nexus_bt_hid_input_dispatch_hogp_notification(
    struct nexus_bt_hid_input_device *device,
    const struct nexus_bt_gatt_hid_service *service,
    const struct nexus_bt_l2cap_view *l2cap);
bool nexus_bt_hid_input_dispatch_hidp_packet(
    struct nexus_bt_hid_input_device *device,
    const uint8_t *packet, size_t packet_length);
/* Emits neutral state before clearing a disconnected peer's held controls. */
void nexus_bt_hid_input_disconnect(struct nexus_bt_hid_input_device *device);

#endif
