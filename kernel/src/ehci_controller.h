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

/* Submit one synchronous high-speed endpoint-zero transfer through the async
 * schedule. Data is bounded to one DMA page; callers receive actual bytes
 * transferred. Low/full-speed TT routing is deliberately not claimed. */
bool ehci_controller_control_transfer(
    uint32_t controller_index, uint8_t device_address,
    uint8_t endpoint_speed, uint16_t maximum_packet,
    const uint8_t setup_packet[8], void *data, uint16_t data_length,
    uint16_t *transferred);

#endif
