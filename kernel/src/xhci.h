#ifndef NEXUS_XHCI_H
#define NEXUS_XHCI_H

#include <stdbool.h>

/* Initialize one xHCI controller and enumerate a directly attached HID
 * boot keyboard. USB hubs and USB mass storage are intentionally unsupported. */
bool xhci_init(void);

/* Drain completed USB keyboard interrupt transfers. Called from the main loop. */
void xhci_poll(void);

#endif
