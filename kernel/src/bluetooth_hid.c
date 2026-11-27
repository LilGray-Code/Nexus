#include "bluetooth_hid.h"

static uint16_t get_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

bool nexus_bt_l2cap_parse(const uint8_t *packet, size_t packet_length,
                          struct nexus_bt_l2cap_view *l2cap) {
    if (packet == NULL || l2cap == NULL || packet_length < 4U)
        return false;
    const uint16_t payload_length = get_le16(packet);
    if ((size_t)payload_length != packet_length - 4U) return false;
    const uint16_t channel_id = get_le16(packet + 2U);
    if (channel_id == 0U) return false;
    *l2cap = (struct nexus_bt_l2cap_view){
        .channel_id = channel_id,
        .payload = packet + 4U,
        .payload_length = payload_length
    };
    return true;
}

bool nexus_bt_hidp_parse_input(const uint8_t *packet, size_t packet_length,
                               struct nexus_bt_hid_report_view *report) {
    if (packet == NULL || report == NULL || packet_length < 2U)
        return false;
    const uint8_t transaction = (uint8_t)(packet[0] >> 4);
    const uint8_t parameter = (uint8_t)(packet[0] & 0x0FU);
    if (transaction != NEXUS_BT_HIDP_TRANSACTION_DATA ||
        parameter != NEXUS_BT_HIDP_REPORT_INPUT) return false;
    *report = (struct nexus_bt_hid_report_view){
        .attribute_handle = 0U,
        .report = packet + 1U,
        .report_length = (uint16_t)(packet_length - 1U)
    };
    return true;
}

bool nexus_bt_hogp_parse_notification(
    const struct nexus_bt_l2cap_view *l2cap,
    struct nexus_bt_hid_report_view *report) {
    if (l2cap == NULL || report == NULL || l2cap->payload == NULL ||
        l2cap->channel_id != NEXUS_BT_L2CAP_CID_ATT ||
        l2cap->payload_length < 4U) return false;
    const uint8_t opcode = l2cap->payload[0];
    if (opcode != NEXUS_BT_ATT_HANDLE_VALUE_NOTIFICATION &&
        opcode != NEXUS_BT_ATT_HANDLE_VALUE_INDICATION) return false;
    const uint16_t handle = get_le16(l2cap->payload + 1U);
    if (handle == 0U) return false;
    const uint16_t value_length = (uint16_t)(l2cap->payload_length - 3U);
    if (value_length == 0U) return false;
    *report = (struct nexus_bt_hid_report_view){
        .attribute_handle = handle,
        .report = l2cap->payload + 3U,
        .report_length = value_length
    };
    return true;
}

bool nexus_bt_hogp_parse_read_response(
    const uint8_t *att_pdu, size_t pdu_length, uint16_t report_handle,
    struct nexus_bt_hid_report_view *report) {
    if (att_pdu == NULL || report == NULL || pdu_length < 2U ||
        report_handle == 0U ||
        att_pdu[0] != NEXUS_BT_ATT_READ_RESPONSE) return false;
    *report = (struct nexus_bt_hid_report_view){
        .attribute_handle = report_handle,
        .report = att_pdu + 1U,
        .report_length = (uint16_t)(pdu_length - 1U)
    };
    return true;
}
