#include "bluetooth_classic_hid.h"

#include <string.h>

#define L2CAP_SIGNAL_COMMAND_REJECT 0x01U
#define L2CAP_SIGNAL_CONNECTION_REQUEST 0x02U
#define L2CAP_SIGNAL_CONNECTION_RESPONSE 0x03U
#define L2CAP_SIGNAL_CONFIG_REQUEST 0x04U
#define L2CAP_SIGNAL_CONFIG_RESPONSE 0x05U
#define L2CAP_CONFIG_SUCCESS 0x0000U
#define L2CAP_CONFIG_UNKNOWN_OPTIONS 0x0003U
#define L2CAP_CONNECTION_SUCCESS 0x0000U
#define HIDP_SET_PROTOCOL_BOOT 0x70U
#define HIDP_HANDSHAKE_SUCCESS 0x00U

static uint16_t get_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void put_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint8_t next_identifier(struct nexus_bt_classic_hid_session *session) {
    ++session->next_identifier;
    if (session->next_identifier == 0U) ++session->next_identifier;
    return session->next_identifier;
}

static bool queue_tx(
    struct nexus_bt_classic_hid_session *session,
    uint16_t channel_id, const uint8_t *payload, size_t length) {
    if (session == NULL || payload == NULL || length == 0U ||
        length > NEXUS_BT_CLASSIC_HID_MAX_CONTROL_PDU) return false;
    const uint8_t next = (uint8_t)(
        (session->tx_tail + 1U) % NEXUS_BT_CLASSIC_HID_TX_DEPTH);
    if (next == session->tx_head) return false;
    struct nexus_bt_classic_hid_tx *item = &session->tx_queue[session->tx_tail];
    item->channel_id = channel_id;
    item->length = (uint16_t)length;
    memcpy(item->payload,payload,length);
    session->tx_tail = next;
    return true;
}

static bool queue_signal(
    struct nexus_bt_classic_hid_session *session,
    uint8_t code, uint8_t identifier,
    const uint8_t *parameters, uint16_t parameter_length) {
    uint8_t packet[NEXUS_BT_CLASSIC_HID_MAX_CONTROL_PDU] = {0U};
    if (identifier == 0U || parameter_length > sizeof(packet) - 4U ||
        (parameter_length != 0U && parameters == NULL)) return false;
    packet[0] = code;
    packet[1] = identifier;
    put_le16(packet + 2U,parameter_length);
    if (parameter_length != 0U)
        memcpy(packet + 4U,parameters,parameter_length);
    return queue_tx(session,NEXUS_BT_CLASSIC_HID_CID_SIGNALING,
                    packet,(size_t)parameter_length + 4U);
}

static void evaluate_state(struct nexus_bt_classic_hid_session *session) {
    if (session == NULL || session->state == NEXUS_BT_CLASSIC_HID_FAILED)
        return;
    const bool connected =
        session->control.connected && session->interrupt.connected;
    const bool configured =
        session->control.local_configured &&
        session->control.peer_configured &&
        session->interrupt.local_configured &&
        session->interrupt.peer_configured;
    if (!connected) {
        session->state = NEXUS_BT_CLASSIC_HID_CHANNELS_PENDING;
    } else if (!configured) {
        session->state = NEXUS_BT_CLASSIC_HID_CHANNELS_CONFIGURING;
    } else if (!session->protocol_accepted) {
        if (!session->protocol_set_queued && !session->protocol_set_sent) {
            const uint8_t command[2] = {HIDP_SET_PROTOCOL_BOOT,0U};
            if (queue_tx(session,session->control.remote_cid,
                         command,sizeof(command)))
                session->protocol_set_queued = true;
        }
        session->state = NEXUS_BT_CLASSIC_HID_PROTOCOL_PENDING;
    } else {
        session->state = NEXUS_BT_CLASSIC_HID_READY;
    }
}

void nexus_bt_classic_hid_init(struct nexus_bt_classic_hid_session *session) {
    if (session == NULL) return;
    *session = (struct nexus_bt_classic_hid_session){0};
    session->control.local_cid = NEXUS_BT_CLASSIC_HID_CID_CONTROL;
    session->interrupt.local_cid = NEXUS_BT_CLASSIC_HID_CID_INTERRUPT;
    session->next_identifier = 1U;
}

bool nexus_bt_classic_hid_start(
    struct nexus_bt_classic_hid_session *session) {
    if (session == NULL) return false;
    nexus_bt_classic_hid_init(session);
    uint8_t parameters[4] = {0U};
    session->control.connection_identifier = next_identifier(session);
    put_le16(parameters,NEXUS_BT_HCI_PSM_HID_CONTROL);
    put_le16(parameters + 2U,session->control.local_cid);
    if (!queue_signal(session,L2CAP_SIGNAL_CONNECTION_REQUEST,
            session->control.connection_identifier,parameters,
            sizeof(parameters))) goto failed;
    session->control.connection_pending = true;

    session->interrupt.connection_identifier = next_identifier(session);
    put_le16(parameters,NEXUS_BT_HCI_PSM_HID_INTERRUPT);
    put_le16(parameters + 2U,session->interrupt.local_cid);
    if (!queue_signal(session,L2CAP_SIGNAL_CONNECTION_REQUEST,
            session->interrupt.connection_identifier,parameters,
            sizeof(parameters))) goto failed;
    session->interrupt.connection_pending = true;
    session->state = NEXUS_BT_CLASSIC_HID_CHANNELS_PENDING;
    return true;
failed:
    session->state = NEXUS_BT_CLASSIC_HID_FAILED;
    return false;
}

bool nexus_bt_classic_hid_next_tx(
    struct nexus_bt_classic_hid_session *session,
    uint16_t *remote_channel_id, uint8_t *payload,
    size_t capacity, size_t *payload_length) {
    if (session == NULL || remote_channel_id == NULL || payload == NULL ||
        payload_length == NULL || session->tx_in_flight ||
        session->tx_head == session->tx_tail) return false;
    const struct nexus_bt_classic_hid_tx *item =
        &session->tx_queue[session->tx_head];
    *payload_length = item->length;
    if (capacity < item->length) return false;
    *remote_channel_id = item->channel_id;
    memcpy(payload,item->payload,item->length);
    session->tx_in_flight = true;
    return true;
}

void nexus_bt_classic_hid_tx_complete(
    struct nexus_bt_classic_hid_session *session, bool success) {
    if (session == NULL || !session->tx_in_flight) return;
    session->tx_in_flight = false;
    if (!success) return;
    const struct nexus_bt_classic_hid_tx *item =
        &session->tx_queue[session->tx_head];
    if (item->channel_id == session->control.remote_cid &&
        item->length == 2U && item->payload[0] == HIDP_SET_PROTOCOL_BOOT) {
        session->protocol_set_queued = false;
        session->protocol_set_sent = true;
    }
    session->tx_head = (uint8_t)(
        (session->tx_head + 1U) % NEXUS_BT_CLASSIC_HID_TX_DEPTH);
    evaluate_state(session);
}

static struct nexus_bt_classic_hid_channel *find_local_channel(
    struct nexus_bt_classic_hid_session *session, uint16_t cid) {
    if (cid == session->control.local_cid) return &session->control;
    if (cid == session->interrupt.local_cid) return &session->interrupt;
    return NULL;
}

static struct nexus_bt_classic_hid_channel *find_channel_by_identifier(
    struct nexus_bt_classic_hid_session *session, uint8_t identifier) {
    if (session->control.connection_pending &&
        session->control.connection_identifier == identifier)
        return &session->control;
    if (session->interrupt.connection_pending &&
        session->interrupt.connection_identifier == identifier)
        return &session->interrupt;
    if (session->control.local_configuration_pending &&
        session->control.configuration_identifier == identifier)
        return &session->control;
    if (session->interrupt.local_configuration_pending &&
        session->interrupt.configuration_identifier == identifier)
        return &session->interrupt;
    return NULL;
}

static bool handle_connection_response(
    struct nexus_bt_classic_hid_session *session,
    uint8_t identifier, const uint8_t *parameters, uint16_t length) {
    if (length != 8U) return false;
    struct nexus_bt_classic_hid_channel *channel =
        find_channel_by_identifier(session,identifier);
    if (channel == NULL || !channel->connection_pending) return false;
    const uint16_t remote_cid = get_le16(parameters);
    const uint16_t local_cid = get_le16(parameters + 2U);
    const uint16_t result = get_le16(parameters + 4U);
    if (local_cid != channel->local_cid ||
        result != L2CAP_CONNECTION_SUCCESS || remote_cid < 0x0040U) {
        session->state = NEXUS_BT_CLASSIC_HID_FAILED;
        return true;
    }
    channel->remote_cid = remote_cid;
    channel->connection_pending = false;
    channel->connected = true;

    uint8_t config[4U] = {0U};
    put_le16(config,channel->remote_cid);
    channel->configuration_identifier = next_identifier(session);
    channel->local_configuration_pending = true;
    if (!queue_signal(session,L2CAP_SIGNAL_CONFIG_REQUEST,
            channel->configuration_identifier,config,sizeof(config))) {
        session->state = NEXUS_BT_CLASSIC_HID_FAILED;
        return false;
    }
    evaluate_state(session);
    return true;
}

static bool handle_config_request(
    struct nexus_bt_classic_hid_session *session,
    uint8_t identifier, const uint8_t *parameters, uint16_t length) {
    if (length < 4U) return false;
    const uint16_t local_cid = get_le16(parameters);
    const uint16_t flags = get_le16(parameters + 2U);
    struct nexus_bt_classic_hid_channel *channel =
        find_local_channel(session,local_cid);
    if (channel == NULL || !channel->connected || flags != 0U) {
        const uint8_t reject[2] = {0U,0U};
        return queue_signal(session,L2CAP_SIGNAL_COMMAND_REJECT,
                            identifier,reject,sizeof(reject));
    }

    size_t offset = 4U;
    while (offset < length) {
        if (offset + 2U > length) return false;
        const uint8_t raw_type = parameters[offset];
        const uint8_t type = raw_type & 0x7FU;
        const uint8_t option_length = parameters[offset + 1U];
        if (offset + 2U + option_length > length) return false;
        if (type == 0x01U && option_length != 2U) return false;
        if (type != 0x01U && (raw_type & 0x80U) == 0U) {
            uint8_t response[32U] = {0U};
            const size_t remaining = (size_t)option_length + 2U;
            if (remaining > sizeof(response) - 6U) return false;
            put_le16(response,channel->local_cid);
            put_le16(response + 2U,0U);
            put_le16(response + 4U,L2CAP_CONFIG_UNKNOWN_OPTIONS);
            memcpy(response + 6U,parameters + offset,remaining);
            return queue_signal(session,L2CAP_SIGNAL_CONFIG_RESPONSE,
                identifier,response,(uint16_t)(remaining + 6U));
        }
        offset += 2U + option_length;
    }
    uint8_t response[6U] = {0U};
    put_le16(response,channel->local_cid);
    put_le16(response + 2U,0U);
    put_le16(response + 4U,L2CAP_CONFIG_SUCCESS);
    if (!queue_signal(session,L2CAP_SIGNAL_CONFIG_RESPONSE,identifier,
                      response,sizeof(response))) return false;
    channel->peer_configured = true;
    evaluate_state(session);
    return true;
}

static bool handle_config_response(
    struct nexus_bt_classic_hid_session *session,
    uint8_t identifier, const uint8_t *parameters, uint16_t length) {
    if (length < 6U) return false;
    struct nexus_bt_classic_hid_channel *channel =
        find_channel_by_identifier(session,identifier);
    if (channel == NULL || !channel->local_configuration_pending)
        return false;
    const uint16_t scid = get_le16(parameters);
    const uint16_t flags = get_le16(parameters + 2U);
    const uint16_t result = get_le16(parameters + 4U);
    if (scid != channel->local_cid || flags != 0U ||
        result != L2CAP_CONFIG_SUCCESS) {
        session->state = NEXUS_BT_CLASSIC_HID_FAILED;
        return true;
    }
    channel->local_configuration_pending = false;
    channel->local_configured = true;
    evaluate_state(session);
    return true;
}

static bool handle_signal(
    struct nexus_bt_classic_hid_session *session,
    const uint8_t *payload, size_t length) {
    size_t offset = 0U;
    while (offset < length) {
        if (offset + 4U > length) return false;
        const uint8_t code = payload[offset];
        const uint8_t identifier = payload[offset + 1U];
        const uint16_t command_length = get_le16(payload + offset + 2U);
        if (identifier == 0U || offset + 4U + command_length > length)
            return false;
        const uint8_t *parameters = payload + offset + 4U;
        bool handled = false;
        switch (code) {
            case L2CAP_SIGNAL_CONNECTION_RESPONSE:
                handled = handle_connection_response(
                    session,identifier,parameters,command_length);
                break;
            case L2CAP_SIGNAL_CONFIG_REQUEST:
                handled = handle_config_request(
                    session,identifier,parameters,command_length);
                break;
            case L2CAP_SIGNAL_CONFIG_RESPONSE:
                handled = handle_config_response(
                    session,identifier,parameters,command_length);
                break;
            default: {
                const uint8_t reject[2] = {0U,0U};
                handled = queue_signal(session,L2CAP_SIGNAL_COMMAND_REJECT,
                    identifier,reject,sizeof(reject));
                break;
            }
        }
        if (!handled) return false;
        offset += 4U + command_length;
    }
    return true;
}

bool nexus_bt_classic_hid_handle_l2cap(
    struct nexus_bt_classic_hid_session *session,
    const uint8_t *l2cap_pdu, size_t pdu_length,
    struct nexus_bt_classic_hid_report *report) {
    if (session == NULL || l2cap_pdu == NULL) return false;
    if (report != NULL) *report = (struct nexus_bt_classic_hid_report){0};
    struct nexus_bt_l2cap_view l2cap = {0};
    if (!nexus_bt_l2cap_parse(l2cap_pdu,pdu_length,&l2cap) ||
        l2cap.payload_length == 0U) return false;

    if (l2cap.channel_id == NEXUS_BT_CLASSIC_HID_CID_SIGNALING)
        return handle_signal(session,l2cap.payload,l2cap.payload_length);

    if (l2cap.channel_id == session->control.local_cid &&
        session->protocol_set_sent && l2cap.payload_length == 1U &&
        (l2cap.payload[0] & 0xF0U) == 0x00U) {
        if (l2cap.payload[0] == HIDP_HANDSHAKE_SUCCESS) {
            session->protocol_accepted = true;
            evaluate_state(session);
        } else {
            session->state = NEXUS_BT_CLASSIC_HID_FAILED;
        }
        return true;
    }

    if (l2cap.channel_id == session->interrupt.local_cid &&
        session->state == NEXUS_BT_CLASSIC_HID_READY &&
        report != NULL) {
        struct nexus_bt_hid_report_view hid = {0};
        if (!nexus_bt_hidp_parse_input(
                l2cap.payload,l2cap.payload_length,&hid)) return false;
        report->valid = true;
        if (hid.report_length == 8U) {
            memcpy(report->keyboard_report,hid.report,8U);
            report->keyboard = true;
        } else if (hid.report_length >= 3U && hid.report_length <= 4U) {
            report->mouse = usb_hid_mouse_decode_report(
                hid.report,hid.report_length,&report->mouse_event);
        }
        return report->keyboard || report->mouse;
    }
    return false;
}

bool nexus_bt_classic_hid_is_ready(
    const struct nexus_bt_classic_hid_session *session) {
    return session != NULL &&
           session->state == NEXUS_BT_CLASSIC_HID_READY &&
           session->protocol_accepted;
}
