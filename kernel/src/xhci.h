#ifndef NEXUS_XHCI_H
#define NEXUS_XHCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct nexus_bt_hci_advertisement;

struct nexus_pci_device;

/* Initialize xHCI and enumerate attached HID devices, supported downstream
 * hubs and recognized USB Bluetooth HCI adapters. Bluetooth USB adapters get
 * independent interrupt event-IN and bulk ACL-IN/OUT endpoints. The HCI host
 * initializes the controller and exposes raw L2CAP send/read operations.
 *
 * This does not imply Bluetooth discovery, pairing/bonding or HOGP/HID report
 * delivery. EHCI/OHCI/UHCI also remain unavailable input controllers; see
 * INPUT-IMPLEMENTATION-STATUS.md for remaining limitations. */
bool xhci_init(void);
bool xhci_controller_owns_pci_device(const struct nexus_pci_device *device);
bool xhci_keyboard_ready(void);
bool xhci_mouse_ready(void);
bool xhci_gamepads_ready(void);

/* Bluetooth HCI USB transport exposes complete L2CAP PDUs to an upper
 * protocol layer. These calls do not imply scanning, pairing, bonding or
 * HOGP/HID report subscription support. */
bool xhci_bluetooth_read_l2cap(uint16_t *connection_handle,
                               uint8_t *pdu, size_t pdu_capacity,
                               size_t *pdu_length);
bool xhci_bluetooth_send_l2cap(uint16_t connection_handle,
                               const uint8_t *pdu, size_t pdu_length);

/* Read a queued legacy LE advertisement discovered by the USB HCI transport.
 * Connect requests use the address/type selected by an upper layer. Pairing
 * and HID GATT subscription are separate, not yet implemented operations. */
bool xhci_bluetooth_read_advertisement(
    struct nexus_bt_hci_advertisement *advertisement);
bool xhci_bluetooth_connect_le(
    uint8_t address_type, const uint8_t address[6]);

/* Drain completed USB HID keyboard and mouse interrupt transfers. */
void xhci_poll(void);

/* Rescan root ports after device errors or power transitions. Resume succeeds
 * only when firmware/platform power management left the controller running. */
void xhci_rescan(void);
bool xhci_resume(void);

#endif
