#ifndef NEXUS_XHCI_H
#define NEXUS_XHCI_H

#include <stdbool.h>

/* Initialize one xHCI controller and enumerate directly attached HID boot
 * keyboards and mice. USB hubs and USB mass storage remain unsupported. */
bool xhci_init(void);
bool xhci_keyboard_ready(void);
bool xhci_mouse_ready(void);

/* Drain completed USB HID keyboard and mouse interrupt transfers. */
void xhci_poll(void);

#endif
