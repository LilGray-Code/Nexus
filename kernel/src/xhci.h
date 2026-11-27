#ifndef NEXUS_XHCI_H
#define NEXUS_XHCI_H

#include <stdbool.h>

struct nexus_pci_device;

/* Initialize xHCI and enumerate directly attached HID keyboards, mice and
 * gamepads. Boot-protocol and a bounded single-report generic HID keyboard/
 * mouse format are normalized into the shared input queue. Up to 32 keyboard
 * slots, 32 mouse slots and 32 gamepad slots are available per xHCI controller.
 * USB hubs, composite multi-interface receivers, multiple report IDs per
 * interface, non-xHCI host controllers and native Bluetooth pairing are not
 * implemented. Compatible direct-root-port devices support hot-plug. */
bool xhci_init(void);
bool xhci_controller_owns_pci_device(const struct nexus_pci_device *device);
bool xhci_keyboard_ready(void);
bool xhci_mouse_ready(void);
bool xhci_gamepads_ready(void);

/* Drain completed USB HID keyboard and mouse interrupt transfers. */
void xhci_poll(void);

#endif
