#ifndef NEXUS_BLUETOOTH_USB_H
#define NEXUS_BLUETOOTH_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bluetooth_hci.h"

/* USB Bluetooth class interface: Wireless Controller / RF / Bluetooth.
 * This module validates endpoint layout and parses/builds raw USB HCI event
 * and ACL frames. xHCI owns endpoint scheduling and HCI controller state;
 * inquiry, pairing/bonding and HID report subscriptions are not provided. */
struct nexus_bluetooth_usb_interface {
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t interrupt_in;
    uint16_t interrupt_in_max_packet;
    uint8_t interrupt_interval;
    uint8_t bulk_in;
    uint16_t bulk_in_max_packet;
    uint8_t bulk_out;
    uint16_t bulk_out_max_packet;
};

bool nexus_bluetooth_usb_find_interface(
    const uint8_t *configuration, size_t length,
    struct nexus_bluetooth_usb_interface *interface);

/* USB HCI interrupt events and bulk ACL packets omit the H4 packet-type byte.
 * These helpers validate framing and adapt the raw USB payloads to the shared
 * HCI parser. ACL builders accept a complete L2CAP PDU including its 4-byte
 * L2CAP header and return the raw USB bulk payload (ACL header + PDU). */
bool nexus_bluetooth_usb_parse_hci_event(
    const uint8_t *usb_event, size_t length,
    struct nexus_bt_hci_event_view *event);
bool nexus_bluetooth_usb_parse_hci_acl(
    const uint8_t *usb_acl, size_t length,
    struct nexus_bt_hci_acl_view *acl);
bool nexus_bluetooth_usb_build_acl_packet(
    uint16_t connection_handle, uint8_t packet_boundary,
    const uint8_t *l2cap_pdu, size_t pdu_length,
    uint8_t *usb_acl, size_t capacity, size_t *usb_acl_length);

#endif
