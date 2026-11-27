#ifndef NEXUS_BLUETOOTH_HID_H
#define NEXUS_BLUETOOTH_HID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_BT_L2CAP_CID_ATT 0x0004U
#define NEXUS_BT_HIDP_TRANSACTION_DATA 0x0AU
#define NEXUS_BT_HIDP_REPORT_INPUT 0x01U
#define NEXUS_BT_ATT_HANDLE_VALUE_NOTIFICATION 0x1BU
#define NEXUS_BT_ATT_HANDLE_VALUE_INDICATION 0x1DU
#define NEXUS_BT_ATT_READ_RESPONSE 0x0BU

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

#endif
