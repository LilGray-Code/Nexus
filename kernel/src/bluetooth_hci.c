#include "bluetooth_hci.h"

#include <string.h>

static uint16_t get_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

bool nexus_bt_hci_parse_event(const uint8_t *packet, size_t packet_length,
                              struct nexus_bt_hci_event_view *event) {
    if (packet == NULL || event == NULL || packet_length < 3U ||
        packet[0] != NEXUS_BT_HCI_PACKET_EVENT) return false;
    if ((size_t)packet[2] != packet_length - 3U) return false;
    *event = (struct nexus_bt_hci_event_view){
        .event_code = packet[1],
        .parameters = packet + 3U,
        .parameter_length = packet[2]
    };
    return true;
}

bool nexus_bt_hci_parse_acl(const uint8_t *packet, size_t packet_length,
                            struct nexus_bt_hci_acl_view *acl) {
    if (packet == NULL || acl == NULL || packet_length < 5U ||
        packet[0] != NEXUS_BT_HCI_PACKET_ACL) return false;
    const uint16_t handle_flags = get_le16(packet + 1U);
    const uint16_t data_length = get_le16(packet + 3U);
    if ((size_t)data_length != packet_length - 5U) return false;

    *acl = (struct nexus_bt_hci_acl_view){
        .connection_handle = (uint16_t)(handle_flags & 0x0FFFU),
        .packet_boundary = (uint8_t)((handle_flags >> 12) & 0x03U),
        .broadcast = (uint8_t)((handle_flags >> 14) & 0x03U),
        .payload = packet + 5U,
        .payload_length = data_length
    };
    return true;
}

bool nexus_bt_hci_connection_complete(
    const struct nexus_bt_hci_event_view *event,
    struct nexus_bt_hci_connection *connection) {
    if (event == NULL || connection == NULL || event->parameters == NULL)
        return false;
    if (event->event_code == NEXUS_BT_HCI_EVENT_CONNECTION_COMPLETE) {
        if (event->parameter_length != 11U) return false;
        if (event->parameters[0] != 0U) return false;
        const uint16_t handle = get_le16(event->parameters + 1U);
        if (handle == 0U || handle > 0x0FFFU) return false;
        *connection = (struct nexus_bt_hci_connection){
            .connected = true,
            .low_energy = false,
            .handle = handle
        };
        for (size_t i = 0U; i < 6U; ++i)
            connection->peer_address[i] = event->parameters[3U + i];
        return true;
    }
    if (event->event_code == NEXUS_BT_HCI_EVENT_LE_META) {
        if (event->parameter_length != 19U ||
            event->parameters[0] != NEXUS_BT_HCI_LE_CONNECTION_COMPLETE ||
            event->parameters[1] != 0U) return false;
        const uint16_t handle = get_le16(event->parameters + 2U);
        if (handle == 0U || handle > 0x0FFFU) return false;
        *connection = (struct nexus_bt_hci_connection){
            .connected = true,
            .low_energy = true,
            .handle = handle
        };
        /* LE connection complete uses address at parameters[5..10]. */
        for (size_t i = 0U; i < 6U; ++i)
            connection->peer_address[i] = event->parameters[6U + i];
        return true;
    }
    return false;
}

bool nexus_bt_hci_disconnection_complete(
    const struct nexus_bt_hci_event_view *event,
    uint16_t *connection_handle, uint8_t *reason) {
    if (event == NULL || connection_handle == NULL || reason == NULL ||
        event->event_code != NEXUS_BT_HCI_EVENT_DISCONNECTION_COMPLETE ||
        event->parameters == NULL || event->parameter_length != 4U ||
        event->parameters[0] != 0U) return false;
    const uint16_t handle = get_le16(event->parameters + 1U);
    if (handle == 0U || handle > 0x0FFFU) return false;
    *connection_handle = handle;
    *reason = event->parameters[3U];
    return true;
}
