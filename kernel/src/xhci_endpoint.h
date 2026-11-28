#ifndef NEXUS_XHCI_ENDPOINT_H
#define NEXUS_XHCI_ENDPOINT_H

#include <stdbool.h>
#include <stdint.h>

enum nexus_xhci_endpoint_transfer {
    NEXUS_XHCI_ENDPOINT_BULK = 0U,
    NEXUS_XHCI_ENDPOINT_INTERRUPT = 1U
};

enum nexus_xhci_endpoint_direction {
    NEXUS_XHCI_ENDPOINT_OUT = 0U,
    NEXUS_XHCI_ENDPOINT_IN = 1U
};

/* Normalized endpoint-context words for one non-control endpoint. */
struct nexus_xhci_endpoint_context {
    uint8_t endpoint_id;
    uint8_t context_entries;
    uint32_t dword0;
    uint32_t dword1;
    uint32_t dword2;
    uint32_t dword3;
    uint32_t dword4;
};

/* Convert USB bEndpointAddress plus transfer characteristics into xHCI DCI
 * and endpoint-context words. interval is already encoded for the xHCI speed;
 * bulk endpoints require interval=0. A transfer-ring dequeue pointer must be
 * 16-byte aligned and below the xHCI 52-bit pointer limit. */
bool nexus_xhci_build_endpoint_context(
    uint8_t endpoint_address,
    enum nexus_xhci_endpoint_transfer transfer,
    uint8_t interval, uint8_t max_burst_size,
    uint16_t max_packet_size, uint64_t transfer_ring_physical,
    uint32_t average_trb_length, uint8_t existing_context_entries,
    struct nexus_xhci_endpoint_context *context);

#endif
