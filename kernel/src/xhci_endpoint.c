#include "xhci_endpoint.h"

#define XHCI_ENDPOINT_RING_ADDRESS_MAX ((1ULL << 52) - 1ULL)

bool nexus_xhci_build_endpoint_context(
    uint8_t endpoint_address,
    enum nexus_xhci_endpoint_transfer transfer,
    uint8_t interval, uint8_t max_burst_size,
    uint16_t max_packet_size, uint64_t transfer_ring_physical,
    uint32_t average_trb_length, uint8_t existing_context_entries,
    struct nexus_xhci_endpoint_context *context) {
    if (context == NULL || transfer > NEXUS_XHCI_ENDPOINT_INTERRUPT ||
        (endpoint_address & 0x70U) != 0U ||
        (endpoint_address & 0x0FU) == 0U ||
        max_burst_size > 15U || max_packet_size == 0U ||
        max_packet_size > 1024U ||
        transfer_ring_physical == 0U ||
        (transfer_ring_physical & 0x0FU) != 0U ||
        transfer_ring_physical > XHCI_ENDPOINT_RING_ADDRESS_MAX ||
        average_trb_length == 0U || average_trb_length > 0xFFFFU ||
        existing_context_entries > 31U) return false;

    if (transfer == NEXUS_XHCI_ENDPOINT_BULK && interval != 0U)
        return false;
    if (transfer == NEXUS_XHCI_ENDPOINT_INTERRUPT && interval > 15U)
        return false;

    const uint8_t endpoint_number = endpoint_address & 0x0FU;
    const enum nexus_xhci_endpoint_direction direction =
        (endpoint_address & 0x80U) != 0U
            ? NEXUS_XHCI_ENDPOINT_IN : NEXUS_XHCI_ENDPOINT_OUT;
    const uint8_t endpoint_id = (uint8_t)(
        endpoint_number * 2U + (uint8_t)direction);
    if (endpoint_id < 2U || endpoint_id > 31U) return false;

    uint8_t endpoint_type = 0U;
    if (transfer == NEXUS_XHCI_ENDPOINT_BULK) {
        endpoint_type = direction == NEXUS_XHCI_ENDPOINT_IN ? 6U : 2U;
    } else {
        endpoint_type = direction == NEXUS_XHCI_ENDPOINT_IN ? 7U : 3U;
    }
    const uint8_t context_entries =
        existing_context_entries > endpoint_id
            ? existing_context_entries : endpoint_id;

    *context = (struct nexus_xhci_endpoint_context){
        .endpoint_id = endpoint_id,
        .context_entries = context_entries,
        .dword0 = (uint32_t)interval << 16,
        .dword1 = (3U << 1) | ((uint32_t)endpoint_type << 3) |
                  ((uint32_t)max_burst_size << 8) |
                  ((uint32_t)max_packet_size << 16),
        .dword2 = (uint32_t)transfer_ring_physical | 1U,
        .dword3 = (uint32_t)(transfer_ring_physical >> 32),
        .dword4 = average_trb_length
    };
    return true;
}
