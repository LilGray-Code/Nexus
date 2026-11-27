#ifndef NEXUS_BLUETOOTH_HCI_H
#define NEXUS_BLUETOOTH_HCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* HCI packet type values used by USB/UART HCI transports. */
#define NEXUS_BT_HCI_PACKET_COMMAND 0x01U
#define NEXUS_BT_HCI_PACKET_ACL     0x02U
#define NEXUS_BT_HCI_PACKET_EVENT   0x04U
#define NEXUS_BT_HCI_EVENT_CONNECTION_COMPLETE 0x03U
#define NEXUS_BT_HCI_EVENT_DISCONNECTION_COMPLETE 0x05U
#define NEXUS_BT_HCI_EVENT_LE_META 0x3EU
#define NEXUS_BT_HCI_LE_CONNECTION_COMPLETE 0x01U
#define NEXUS_BT_HCI_PSM_HID_CONTROL 0x0011U
#define NEXUS_BT_HCI_PSM_HID_INTERRUPT 0x0013U
#define NEXUS_BT_HCI_CID_ATT 0x0004U
#define NEXUS_BT_HID_SERVICE_UUID 0x1812U
#define NEXUS_BT_HID_REPORT_MAP_UUID 0x2A4BU
#define NEXUS_BT_HID_REPORT_UUID 0x2A4DU

struct nexus_bt_hci_event_view {
    uint8_t event_code;
    const uint8_t *parameters;
    uint8_t parameter_length;
};

struct nexus_bt_hci_acl_view {
    uint16_t connection_handle;
    uint8_t packet_boundary;
    uint8_t broadcast;
    const uint8_t *payload;
    uint16_t payload_length;
};

struct nexus_bt_hci_connection {
    bool connected;
    bool low_energy;
    uint16_t handle;
    uint8_t peer_address[6];
};

/* Strict HCI framing validation: the transport packet must contain exactly
 * the declared event/ACL payload, with no truncated or overlong packet. */
bool nexus_bt_hci_parse_event(const uint8_t *packet, size_t packet_length,
                              struct nexus_bt_hci_event_view *event);
bool nexus_bt_hci_parse_acl(const uint8_t *packet, size_t packet_length,
                            struct nexus_bt_hci_acl_view *acl);
bool nexus_bt_hci_connection_complete(
    const struct nexus_bt_hci_event_view *event,
    struct nexus_bt_hci_connection *connection);
bool nexus_bt_hci_disconnection_complete(
    const struct nexus_bt_hci_event_view *event,
    uint16_t *connection_handle, uint8_t *reason);

#endif
