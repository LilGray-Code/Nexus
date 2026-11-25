#ifndef NEXUS_XHCI_H
#define NEXUS_XHCI_H

#include <stdbool.h>

/* Initialize xHCI and enumerate boot keyboards/mice plus generic HID gamepads.
 * Root-port connect/disconnect events are handled after boot, so wired devices
 * and standard 2.4 GHz USB receivers can be attached or removed at runtime.
 * Up to four generic report-descriptor gamepads are tracked. Bluetooth pairing,
 * USB hubs, composite multi-interface controllers, and non-gamepad HID remain
 * unsupported; a wireless USB receiver must expose a compatible HID interface. */
bool xhci_init(void);
bool xhci_keyboard_ready(void);
bool xhci_mouse_ready(void);
bool xhci_gamepads_ready(void);

/* Drain completed USB HID keyboard and mouse interrupt transfers. */
void xhci_poll(void);

#endif
