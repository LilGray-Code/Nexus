#ifndef NEXUS_XHCI_H
#define NEXUS_XHCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct nexus_bt_hci_advertisement;

struct nexus_bt_runtime_status {
    uint8_t adapter_count;
    uint8_t ready_adapters;
    uint8_t scanning_adapters;
    uint8_t connected_links;
    uint8_t encrypted_links;
    uint8_t hogp_ready_links;
    uint32_t dropped_packets;
};

struct nexus_pci_device;

/* Initialize xHCI and enumerate attached HID devices, supported downstream
 * hubs and recognized USB Bluetooth HCI adapters. The USB HCI transport
 * performs passive LE discovery and connection initiation. Connected LE
 * peers are queried for HOGP services, input Report Map/characteristics and
 * CCCD subscriptions; supported HID keyboard/mouse notifications feed the
 * common input queue.
 *
 * Before enabling an input-report CCCD, the adapter requires LE encryption.
 * It attempts Secure Connections with the controller P-256/DHKey engine and
 * f4/f5/f6 transcript verification; a peer that explicitly declines SC can
 * continue only that same exchange using non-bonding legacy Just Works.
 * NoInputNoOutput means there is no MITM protection, passkey/OOB/numeric
 * confirmation UI, or persistent bond store. The Settings view exposes
 * adapter status and passive LE advertisements. Classic BR/EDR HIDP is not
 * connected to the input runtime. EHCI/OHCI/UHCI are not fully operational
 * input controllers. */
bool xhci_init(void);
bool xhci_controller_owns_pci_device(const struct nexus_pci_device *device);
bool xhci_keyboard_ready(void);
bool xhci_mouse_ready(void);
bool xhci_gamepads_ready(void);

/* Bluetooth HCI USB transport exposes complete L2CAP PDUs to an upper
 * protocol layer. Internal ATT/HOGP and BLE SC/legacy pairing share this
 * transport. Pairing uses NoInputNoOutput Just Works only; there is no MITM
 * confirmation, passkey/OOB, persistent bond store or classic HIDP client. */
bool xhci_bluetooth_read_l2cap(uint16_t *connection_handle,
                               uint8_t *pdu, size_t pdu_capacity,
                               size_t *pdu_length);
bool xhci_bluetooth_send_l2cap(uint16_t connection_handle,
                               const uint8_t *pdu, size_t pdu_length);

/* Read a queued legacy LE advertisement discovered by the USB HCI transport.
 * Connect requests use the address/type selected by an upper layer. Security
 * is initiated on peer request/ATT permission error or before CCCD subscription;
 * encrypted links are required before wireless HID reports enter input. */
bool xhci_bluetooth_read_advertisement(
    struct nexus_bt_hci_advertisement *advertisement);
bool xhci_bluetooth_connect_le(
    uint8_t address_type, const uint8_t address[6]);
bool xhci_bluetooth_start_scan(void);
bool xhci_bluetooth_get_status(struct nexus_bt_runtime_status *status);

/* Drain completed USB HID keyboard and mouse interrupt transfers. */
void xhci_poll(void);

/* Rescan root ports after device errors or power transitions. Resume succeeds
 * only when firmware/platform power management left the controller running. */
void xhci_rescan(void);
bool xhci_resume(void);

#endif
