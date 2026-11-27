#include "xhci_ports.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    uint64_t offset = 0U;

    assert(nexus_xhci_root_port_count(0U) == 0U);
    assert(nexus_xhci_root_port_count(1U << 24) == 1U);
    assert(nexus_xhci_root_port_count(32U << 24) == 32U);
    assert(nexus_xhci_root_port_count(255U << 24) == 255U);

    assert(!nexus_xhci_port_register_offset(0x1FU, 1U, &offset));
    assert(!nexus_xhci_port_register_offset(0x40U, 0U, &offset));
    assert(!nexus_xhci_port_register_offset(0x40U, 256U, &offset));
    assert(!nexus_xhci_port_register_offset(0x40U, 1U, NULL));

    assert(nexus_xhci_port_register_offset(0x40U, 1U, &offset));
    assert(offset == 0x440U);
    assert(nexus_xhci_port_register_offset(0x40U, 32U, &offset));
    assert(offset == 0x630U);
    assert(nexus_xhci_port_register_offset(0x40U, 255U, &offset));
    assert(offset == 0x1420U);

    assert(!nexus_xhci_port_connected(0U));
    assert(nexus_xhci_port_connected(1U));
    assert(nexus_xhci_port_speed_id(3U << 10) == 3U);
    assert(nexus_xhci_port_speed_id(5U << 10) == 5U);
    assert(nexus_xhci_port_change_bits((1U << 17) | (1U << 22)) ==
           ((1U << 17) | (1U << 22)));

    puts("NEXUS XHCI ROOT PORT RANGE AND PORTSC HELPERS PASSED");
    return 0;
}
