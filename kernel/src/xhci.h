#ifndef NEXUS_XHCI_H
#define NEXUS_XHCI_H

#include <stdbool.h>

/* Initialize an xHCI controller and enumerate USB HID boot keyboards/mice.
 * Root-port connect/disconnect events are handled after boot, so wired devices
 * and standard 2.4 GHz USB receivers can be attached or removed at runtime.
 * Bluetooth pairing, USB hubs, multi-interface receivers, and non-boot HID
 * remain unsupported; a Bluetooth receiver must itself expose boot-HID. */
bool xhci_init(void);
bool xhci_keyboard_ready(void);
bool xhci_mouse_ready(void);

/* Drain completed USB HID keyboard and mouse interrupt transfers. */
void xhci_poll(void);

#endif
