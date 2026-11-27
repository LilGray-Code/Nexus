#include "bluetooth_usb.h"

#include <assert.h>
#include <stdio.h>

static const uint8_t bluetooth_config[] = {
    9,2, 39,0, 1,1,0,0x80,50,
    9,4, 0,0,3,0xE0,0x01,0x01,0,
    7,5, 0x81,3,16,0,1,
    7,5, 0x82,2,64,0,0,
    7,5, 0x02,2,64,0,0
};

int main(void) {
    struct nexus_bluetooth_usb_interface interface = {0};
    assert(nexus_bluetooth_usb_find_interface(
        bluetooth_config, sizeof(bluetooth_config), &interface));
    assert(interface.interface_number == 0U);
    assert(interface.interrupt_in == 0x81U);
    assert(interface.interrupt_in_max_packet == 16U);
    assert(interface.bulk_in == 0x82U);
    assert(interface.bulk_in_max_packet == 64U);
    assert(interface.bulk_out == 0x02U);
    assert(interface.bulk_out_max_packet == 64U);
    assert(!nexus_bluetooth_usb_find_interface(bluetooth_config,
        sizeof(bluetooth_config)-1U, &interface));
    puts("NEXUS USB BLUETOOTH HCI ENDPOINT DISCOVERY TESTS PASSED");
    return 0;
}
