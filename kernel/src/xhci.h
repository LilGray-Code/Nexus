#ifndef NEXUS_XHCI_H
#define NEXUS_XHCI_H

#include <stdbool.h>

struct nexus_pci_device;

/* Initialize xHCI and enumerate directly attached and supported downstream
 * HID keyboards, mice and gamepads. The controller scans its advertised root
 * ports, configures USB 2/SuperSpeed hubs, and monitors hub interrupt-IN
 * change notifications with a GET_STATUS polling fallback. Composite input
 * devices can expose up to 16 discovered HID interfaces; standard keyboard/
 * mouse endpoints receive independent rings and report state.
 *
 * This API does not imply support for legacy EHCI/OHCI/UHCI controllers or
 * Bluetooth HID pairing/report delivery. See INPUT-IMPLEMENTATION-STATUS.md
 * for the remaining controller, protocol and physical-hardware limitations. */
bool xhci_init(void);
bool xhci_controller_owns_pci_device(const struct nexus_pci_device *device);
bool xhci_keyboard_ready(void);
bool xhci_mouse_ready(void);
bool xhci_gamepads_ready(void);

/* Drain completed USB HID keyboard and mouse interrupt transfers. */
void xhci_poll(void);

/* Rescan root ports after device errors or power transitions. Resume succeeds
 * only when firmware/platform power management left the controller running. */
void xhci_rescan(void);
bool xhci_resume(void);

#endif
