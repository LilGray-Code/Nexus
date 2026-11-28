#ifndef NEXUS_BLUETOOTH_USB_H
#define NEXUS_BLUETOOTH_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bluetooth_hci.h"

/* USB Bluetooth class interface: Wireless Controller / RF / Bluetooth. */
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

#define NEXUS_BLUETOOTH_USB_MAX_EVENT_BYTES 257U
#define NEXUS_BLUETOOTH_USB_MAX_ACL_BYTES \
    (NEXUS_BT_HCI_MAX_L2CAP_PACKET + 4U)

struct nexus_bluetooth_usb_io {
    void *context;
    bool (*control_out)(
        void *context, uint8_t request_type, uint8_t request,
        uint16_t value, uint16_t index,
        const uint8_t *data, size_t length);
    bool (*interrupt_in)(
        void *context, uint8_t endpoint, uint8_t *data,
        size_t capacity, size_t *received);
    bool (*bulk_in)(
        void *context, uint8_t endpoint, uint8_t *data,
        size_t capacity, size_t *received);
    bool (*bulk_out)(
        void *context, uint8_t endpoint,
        const uint8_t *data, size_t length);
};

struct nexus_bluetooth_usb_transport {
    bool initialized;
    struct nexus_bluetooth_usb_interface interface;
    struct nexus_bluetooth_usb_io io;
    struct nexus_bt_hci_acl_reassembler acl_reassembler;
};

/* Bind discovered endpoint addresses to a USB I/O backend. The backend
 * schedules the actual control, interrupt-IN and bulk transfers. */
bool nexus_bluetooth_usb_transport_init(
    struct nexus_bluetooth_usb_transport *transport,
    const struct nexus_bluetooth_usb_interface *interface,
    const struct nexus_bluetooth_usb_io *io);
bool nexus_bluetooth_usb_send_hci_command(
    struct nexus_bluetooth_usb_transport *transport,
    const uint8_t *hci_packet, size_t packet_length);
bool nexus_bluetooth_usb_read_hci_event(
    struct nexus_bluetooth_usb_transport *transport,
    uint8_t *hci_packet, size_t capacity, size_t *packet_length);
bool nexus_bluetooth_usb_send_acl(
    struct nexus_bluetooth_usb_transport *transport,
    const uint8_t *hci_packet, size_t packet_length);
bool nexus_bluetooth_usb_poll_acl(
    struct nexus_bluetooth_usb_transport *transport,
    struct nexus_bt_hci_l2cap_packet *completed,
    enum nexus_bt_hci_acl_reassembly_status *status);

#endif
