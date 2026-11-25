#ifndef NEXUS_XHCI_H
#define NEXUS_XHCI_H

#include <stdbool.h>

/* Initialize one xHCI controller and enumerate directly attached USB HID
 * boot keyboards and mice. A 2.4 GHz wireless receiver works only when it
 * exposes the same standard boot-HID interfaces. Bluetooth pairing, USB hubs,
 * multi-interface receivers, and non-boot-protocol HID remain unsupported. */
bool xhci_init(void);
bool xhci_keyboard_ready(void);
bool xhci_mouse_ready(void);

/* Drain completed USB HID keyboard and mouse interrupt transfers. */
void xhci_poll(void);

#endif
