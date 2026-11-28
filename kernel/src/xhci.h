#ifndef NEXUS_XHCI_H
#define NEXUS_XHCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct nexus_bt_hci_advertisement;
struct nexus_bt_hci_classic_device;

struct nexus_bt_runtime_status {
    uint8_t adapter_count;
    uint8_t ready_adapters;
    uint8_t scanning_adapters;
    uint8_t connected_links;
    uint8_t encrypted_links;
    uint8_t hogp_ready_links;
    uint8_t classic_scanning_adapters;
    uint8_t classic_hid_ready_links;
    uint8_t classic_device_count;
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
 * adapter status, LE advertisements and Classic BR/EDR inquiry results.
 * Classic HIDP is connected only for links the controller reports encrypted;
 * Classic SSP pairing, MITM UI and persistent link-key storage are not
 * provided. EHCI/OHCI/UHCI are not fully operational input controllers. */
bool xhci_init(void);
bool xhci_controller_owns_pci_device(const struct nexus_pci_device *device);
bool xhci_keyboard_ready(void);
bool xhci_mouse_ready(void);
bool xhci_gamepads_ready(void);

/* Bluetooth HCI USB transport exposes complete L2CAP PDUs to an upper
 * protocol layer. Internal ATT/HOGP, BLE SC/legacy pairing, and Classic HIDP
 * over controller-reported encrypted BR/EDR links share this transport.
 * Pairing uses NoInputNoOutput Just Works for BLE only; Classic SSP, MITM
 * confirmation and persistent link-key storage are not implemented. */
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

/* Classic BR/EDR discovery is independent of passive LE scan. Classic HIDP
 * channels are started only for links reported encrypted by the controller;
 * this runtime does not implement SSP passkey/MITM UI or persistent keys. */
bool xhci_bluetooth_start_classic_inquiry(void);
size_t xhci_bluetooth_classic_device_count(void);
bool xhci_bluetooth_get_classic_device(
    size_t index, struct nexus_bt_hci_classic_device *device);
bool xhci_bluetooth_connect_classic(size_t index);

/* Drain completed USB HID keyboard and mouse interrupt transfers. */
void xhci_poll(void);

/* Rescan root ports after device errors or power transitions. Resume succeeds
 * only when firmware/platform power management left the controller running. */
void xhci_rescan(void);
bool xhci_resume(void);

#endif
