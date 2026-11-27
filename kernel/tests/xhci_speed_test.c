#include "xhci_speed.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    assert(!nexus_xhci_speed_supported(0U));
    for (uint8_t speed = 1U; speed <= 5U; ++speed)
        assert(nexus_xhci_speed_supported(speed));
    assert(!nexus_xhci_speed_supported(6U));
    assert(!nexus_xhci_speed_supported(15U));

    assert(!nexus_xhci_speed_is_superspeed(1U));
    assert(!nexus_xhci_speed_is_superspeed(2U));
    assert(!nexus_xhci_speed_is_superspeed(3U));
    assert(nexus_xhci_speed_is_superspeed(4U));
    assert(nexus_xhci_speed_is_superspeed(5U));
    assert(!nexus_xhci_speed_requires_warm_reset(3U));
    assert(nexus_xhci_speed_requires_warm_reset(4U));
    assert(nexus_xhci_speed_requires_warm_reset(5U));

    assert(nexus_xhci_default_ep0_packet_size(1U) == 8U);
    assert(nexus_xhci_default_ep0_packet_size(2U) == 8U);
    assert(nexus_xhci_default_ep0_packet_size(3U) == 64U);
    assert(nexus_xhci_default_ep0_packet_size(4U) == 512U);
    assert(nexus_xhci_default_ep0_packet_size(5U) == 512U);

    assert(nexus_xhci_descriptor_ep0_packet_size(1U, 8U) == 8U);
    assert(nexus_xhci_descriptor_ep0_packet_size(2U, 16U) == 16U);
    assert(nexus_xhci_descriptor_ep0_packet_size(3U, 64U) == 64U);
    assert(nexus_xhci_descriptor_ep0_packet_size(4U, 9U) == 512U);
    assert(nexus_xhci_descriptor_ep0_packet_size(5U, 9U) == 512U);
    assert(nexus_xhci_descriptor_ep0_packet_size(2U, 0U) == 8U);

    puts("NEXUS XHCI USB 2.0/3.0/3.1 SPEED AND EP0 TESTS PASSED");
    return 0;
}
