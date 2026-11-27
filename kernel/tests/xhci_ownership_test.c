#include "xhci_ownership.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    assert(nexus_xhci_extended_cap_start(0U) == 0U);
    assert(nexus_xhci_extended_cap_start(0x00010000U) == 4U);
    assert(nexus_xhci_extended_cap_start(0x12340000U) == 0x48D0U);

    assert(nexus_xhci_extended_cap_next(0x40U, 0x00000401U) == 0x50U);
    assert(nexus_xhci_extended_cap_next(0x40U, 0x00000001U) == 0U);
    assert(nexus_xhci_extended_cap_next(0xFFFFFFFCU, 0x00000401U) == 0U);
    assert(nexus_xhci_legacy_capability_id(0x12340401U) == 1U);
    assert(nexus_xhci_legacy_capability_id(0x12340402U) == 2U);

    assert(nexus_xhci_request_os_ownership(0U) == 1U);
    assert(nexus_xhci_request_os_ownership(1U) == 1U);
    assert(nexus_xhci_bios_ownership_released(0U));
    assert(!nexus_xhci_bios_ownership_released(1U));

    const uint32_t enables =
        (1U << 0) | (1U << 4) | (1U << 13) | (1U << 14) | (1U << 15);
    const uint32_t event_latches =
        (1U << 29) | (1U << 30) | (1U << 31);
    const uint32_t read_only_status = (1U << 16) | (1U << 20);
    const uint32_t unrelated = (1U << 7) | (1U << 24);

    const uint32_t disabled =
        nexus_xhci_disable_legacy_smi(enables | read_only_status | unrelated);
    assert((disabled & enables) == 0U);
    assert((disabled & event_latches) == event_latches);
    assert((disabled & read_only_status) == read_only_status);
    assert((disabled & unrelated) == unrelated);
    assert(nexus_xhci_disable_legacy_smi(disabled) == disabled);

    puts("NEXUS XHCI BIOS/OS OWNERSHIP AND SMI MASK TESTS PASSED");
    return 0;
}
