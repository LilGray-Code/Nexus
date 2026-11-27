#include "usb_hub_requests.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    struct nexus_usb_setup_packet setup = {0};
    assert(nexus_usb_hub_get_descriptor_request(false, &setup));
    assert(setup.request_type == 0xA0U);
    assert(setup.request == 0x06U && setup.value == 0x2900U);
    assert(nexus_usb_hub_get_descriptor_request(true, &setup));
    assert(setup.value == 0x2A00U);

    assert(nexus_usb_hub_get_port_status_request(3U, &setup));
    assert(setup.request_type == 0xA3U && setup.request == 0x00U);
    assert(setup.index == 3U && setup.length == 4U);
    assert(!nexus_usb_hub_get_port_status_request(0U, &setup));

    assert(nexus_usb_hub_set_port_feature_request(
        2U, NEXUS_USB_HUB_FEATURE_PORT_POWER, &setup));
    assert(setup.request_type == 0x23U && setup.request == 0x03U);
    assert(setup.index == 2U && setup.value == 8U && setup.length == 0U);

    assert(nexus_usb_hub_set_port_feature_request(
        2U, NEXUS_USB_HUB_FEATURE_PORT_RESET, &setup));
    assert(setup.value == 4U);

    assert(nexus_usb_hub_clear_port_feature_request(
        2U, NEXUS_USB_HUB_FEATURE_C_PORT_CONNECTION, &setup));
    assert(setup.request == 0x01U && setup.value == 16U);
    assert(!nexus_usb_hub_clear_port_feature_request(0U, 16U, &setup));

    puts("NEXUS USB HUB CLASS REQUEST TESTS PASSED");
    return 0;
}
