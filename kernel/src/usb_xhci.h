#ifndef NEXUS_USB_XHCI_H
#define NEXUS_USB_XHCI_H

#include <stdbool.h>
#include <stdint.h>

struct nexus_xhci_bulk_trb {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
};

/*
 * Build the five dwords of an xHCI bulk endpoint context. The caller supplies
 * a 16-byte-aligned transfer-ring DMA address and installs the context using
 * Configure Endpoint. No controller registers are touched here.
 */
bool nexus_xhci_bulk_endpoint_context(
    uint8_t endpoint_address, uint16_t max_packet, uint64_t ring_physical,
    uint32_t context[5], uint8_t *endpoint_id);

/*
 * Build one normal TRB for a bulk transfer. The caller owns the DMA buffer,
 * ring cycle state, doorbell, and completion event. Length is limited to the
 * xHCI Normal TRB transfer-length field.
 */
bool nexus_xhci_bulk_transfer_trb(
    uint8_t endpoint_address, uint64_t buffer_physical, uint32_t length,
    struct nexus_xhci_bulk_trb *trb);

#endif
