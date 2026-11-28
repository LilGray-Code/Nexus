#ifndef NEXUS_BLUETOOTH_CLASSIC_HID_H
#define NEXUS_BLUETOOTH_CLASSIC_HID_H

#include "bluetooth_hci.h"
#include "bluetooth_hid.h"
#include "usb_hid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_BT_CLASSIC_HID_TX_DEPTH 8U
#define NEXUS_BT_CLASSIC_HID_MAX_CONTROL_PDU 32U
#define NEXUS_BT_CLASSIC_HID_CID_SIGNALING 0x0001U
#define NEXUS_BT_CLASSIC_HID_CID_CONTROL 0x0040U
#define NEXUS_BT_CLASSIC_HID_CID_INTERRUPT 0x0041U

enum nexus_bt_classic_hid_state {
    NEXUS_BT_CLASSIC_HID_IDLE = 0U,
    NEXUS_BT_CLASSIC_HID_CHANNELS_PENDING,
    NEXUS_BT_CLASSIC_HID_CHANNELS_CONFIGURING,
    NEXUS_BT_CLASSIC_HID_PROTOCOL_PENDING,
    NEXUS_BT_CLASSIC_HID_READY,
    NEXUS_BT_CLASSIC_HID_FAILED
};

struct nexus_bt_classic_hid_tx {
    uint16_t channel_id;
    uint16_t length;
    uint8_t payload[NEXUS_BT_CLASSIC_HID_MAX_CONTROL_PDU];
};

struct nexus_bt_classic_hid_channel {
    uint16_t local_cid;
    uint16_t remote_cid;
    uint8_t connection_identifier;
    uint8_t configuration_identifier;
    bool connection_pending;
    bool connected;
    bool local_configuration_pending;
    bool local_configured;
    bool peer_configured;
};

struct nexus_bt_classic_hid_session {
    enum nexus_bt_classic_hid_state state;
    uint8_t next_identifier;
    struct nexus_bt_classic_hid_channel control;
    struct nexus_bt_classic_hid_channel interrupt;
    bool protocol_set_queued;
    bool protocol_set_sent;
    bool protocol_accepted;
    bool tx_in_flight;
    uint8_t tx_head;
    uint8_t tx_tail;
    struct nexus_bt_classic_hid_tx tx_queue[
        NEXUS_BT_CLASSIC_HID_TX_DEPTH];
};

struct nexus_bt_classic_hid_report {
    bool valid;
    bool keyboard;
    bool mouse;
    uint8_t keyboard_report[8];
    struct nexus_usb_mouse_event mouse_event;
};

void nexus_bt_classic_hid_init(struct nexus_bt_classic_hid_session *session);
bool nexus_bt_classic_hid_start(struct nexus_bt_classic_hid_session *session);
/* A packet remains queued until tx_complete(true); a failed transport
 * completion makes the exact packet available for retry. */
bool nexus_bt_classic_hid_next_tx(
    struct nexus_bt_classic_hid_session *session,
    uint16_t *remote_channel_id, uint8_t *payload,
    size_t capacity, size_t *payload_length);
void nexus_bt_classic_hid_tx_complete(
    struct nexus_bt_classic_hid_session *session, bool success);
/* Consume one complete L2CAP PDU (including its four-byte L2CAP header).
 * Signalling is parsed and answered; HIDP boot reports are returned. */
bool nexus_bt_classic_hid_handle_l2cap(
    struct nexus_bt_classic_hid_session *session,
    const uint8_t *l2cap_pdu, size_t pdu_length,
    struct nexus_bt_classic_hid_report *report);
bool nexus_bt_classic_hid_is_ready(
    const struct nexus_bt_classic_hid_session *session);

#endif
