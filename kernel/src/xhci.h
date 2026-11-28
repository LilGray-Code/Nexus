#ifndef NEXUS_XHCI_H
#define NEXUS_XHCI_H

#include <stdbool.h>

struct nexus_pci_device;

/* Initialize xHCI and enumerate directly attached HID keyboards, mice and
 * gamepads. Scan the controller-advertised root-port range (up to 255 ports),
 * and periodically rescan root ports for missed events/reconnection.
 *
 * Bounded generic keyboard/mouse parsing supports multiple Report IDs per
 * selected HID interface. A composite USB receiver can expose one primary HID
 * interface plus one companion keyboard-or-mouse interface; additional HID
 * interfaces beyond that pair are not dispatched. The xHCI driver is the only
 * operational USB host-controller backend. UHCI/OHCI/EHCI and Bluetooth
 * controller transports are not operational in this kernel build. */
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
