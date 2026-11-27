#ifndef NEXUS_XHCI_H
#define NEXUS_XHCI_H

#include <stdbool.h>

struct nexus_pci_device;

/* Initialize xHCI and enumerate boot keyboards/mice plus generic HID gamepads.
 * Up to four independent boot keyboards, four boot mice and four gamepads can
 * be active. Compatible devices support hot-plug. USB hubs, Bluetooth pairing,
 * composite multi-interface receivers and non-boot keyboard/mouse report formats
 * still need additional USB host/HID driver support. */
bool xhci_init(void);
bool xhci_controller_owns_pci_device(const struct nexus_pci_device *device);
bool xhci_keyboard_ready(void);
bool xhci_mouse_ready(void);
bool xhci_gamepads_ready(void);

/* Drain completed USB HID keyboard and mouse interrupt transfers. */
void xhci_poll(void);

#endif
