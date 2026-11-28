#ifndef NEXUS_XHCI_H
#define NEXUS_XHCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct nexus_bt_hci_advertisement;

struct nexus_pci_device;

/* Initialize xHCI and enumerate attached HID devices, supported downstream
 * hubs and recognized USB Bluetooth HCI adapters. The USB HCI transport
 * performs passive LE discovery and connection initiation. Connected LE
 * peers are queried for HOGP services, input Report Map/characteristics and
 * CCCD subscriptions; supported HID keyboard/mouse notifications feed the
 * common input queue.
 *
 * On ATT security errors, the adapter can attempt non-bonding legacy Just
 * Works pairing and enable link encryption. This mode provides no MITM
 * authentication and no persistent bond; LE Secure Connections, passkey/OOB
 * pairing and MITM-required devices remain unsupported. EHCI/OHCI/UHCI are
 * not yet operational input controllers. */
bool xhci_init(void);
bool xhci_controller_owns_pci_device(const struct nexus_pci_device *device);
bool xhci_keyboard_ready(void);
bool xhci_mouse_ready(void);
bool xhci_gamepads_ready(void);

/* Bluetooth HCI USB transport exposes complete L2CAP PDUs to an upper
 * protocol layer. Internal ATT/HOGP and limited legacy SMP handling share
 * this transport; these calls do not imply authenticated pairing, bonding
 * or classic HIDP support. */
bool xhci_bluetooth_read_l2cap(uint16_t *connection_handle,
                               uint8_t *pdu, size_t pdu_capacity,
                               size_t *pdu_length);
bool xhci_bluetooth_send_l2cap(uint16_t connection_handle,
                               const uint8_t *pdu, size_t pdu_length);

/* Read a queued legacy LE advertisement discovered by the USB HCI transport.
 * Connect requests use the address/type selected by an upper layer. A security
 * error can trigger non-bonding legacy Just Works; authenticated pairing and
 * bonding remain unsupported. */
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
