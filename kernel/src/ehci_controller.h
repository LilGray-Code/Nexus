#ifndef NEXUS_EHCI_CONTROLLER_H
#define NEXUS_EHCI_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

/* Bring up recognized EHCI PCI controllers in polling mode. This owns the
 * controller, performs firmware handoff/reset, and starts an empty asynchronous
 * schedule. USB address/configuration/endpoint transfers are not yet exposed. */
bool ehci_controller_init_all(void);

/* Poll controller health and root-port connection changes. */
void ehci_controller_poll(void);

/* Number of EHCI controllers retained, including quarantined DMA failures. */
uint32_t ehci_controller_count(void);
uint32_t ehci_controller_running_count(void);

#endif
