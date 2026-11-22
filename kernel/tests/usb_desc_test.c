#include "usb_desc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_bulk_pair_on_matching_interface(void) {
    const uint8_t config[] = {
        9,2,32,0,1,1,0,0x80,50,
        9,4,2,0,2,0xFF,0x42,1,0,
        7,5,0x81,2,0,2,0,
        7,5,0x02,2,0,2,0
    };
    struct nexus_usb_bulk_interface found;
    memset(&found, 0, sizeof(found));
    assert(nexus_usb_find_bulk_interface(config, sizeof(config),
        0xFF, 0x42, 1, &found));
    assert(found.configuration_value == 1);
    assert(found.interface_number == 2);
    assert(found.alternate_setting == 0);
    assert(found.interface_class == 0xFF);
    assert(found.bulk_in_endpoint == 0x81);
    assert(found.bulk_out_endpoint == 0x02);
    assert(found.bulk_in_max_packet == 512);
    assert(found.bulk_out_max_packet == 512);
}

static void test_does_not_pair_endpoints_across_interfaces(void) {
    const uint8_t config[] = {
        9,2,39,0,2,1,0,0x80,50,
        9,4,0,0,1,0xFF,1,1,0,
        7,5,0x81,2,64,0,0,
        9,4,1,0,1,0xFF,1,1,0,
        7,5,0x02,2,64,0,0
    };
    struct nexus_usb_bulk_interface found;
    assert(!nexus_usb_find_bulk_interface(config, sizeof(config),
        0xFF, 1, 1, &found));
}

static void test_ignores_interrupt_endpoints_and_wrong_class(void) {
    const uint8_t config[] = {
        9,2,32,0,1,1,0,0x80,50,
        9,4,0,0,2,3,1,1,0,
        7,5,0x81,3,8,0,10,
        7,5,0x02,2,64,0,0
    };
    struct nexus_usb_bulk_interface found;
    assert(!nexus_usb_find_bulk_interface(config, sizeof(config),
        0xFF, 0x42, 1, &found));
}

static void test_rejects_malformed_descriptors(void) {
    const uint8_t truncated[] = {9,2,18,0,1,1,0,0x80,50,9,4,0};
    const uint8_t short_interface[] = {
        9,2,18,0,1,1,0,0x80,50,2,4
    };
    struct nexus_usb_bulk_interface found;
    assert(!nexus_usb_find_bulk_interface(truncated, sizeof(truncated),
        0xFF, 0xFF, 0xFF, &found));
    assert(!nexus_usb_find_bulk_interface(short_interface,
        sizeof(short_interface), 0xFF, 0xFF, 0xFF, &found));
    assert(!nexus_usb_find_bulk_interface(NULL, 0, 0xFF, 0xFF, 0xFF, &found));
}

static void test_does_not_accept_zero_packet_endpoint(void) {
    const uint8_t config[] = {
        9,2,32,0,1,1,0,0x80,50,
        9,4,0,0,2,0xFF,1,1,0,
        7,5,0x81,2,0,0,0,
        7,5,0x02,2,64,0,0
    };
    struct nexus_usb_bulk_interface found;
    assert(!nexus_usb_find_bulk_interface(config, sizeof(config),
        0xFF, 1, 1, &found));
}

int main(void) {
    test_bulk_pair_on_matching_interface();
    test_does_not_pair_endpoints_across_interfaces();
    test_ignores_interrupt_endpoints_and_wrong_class();
    test_rejects_malformed_descriptors();
    test_does_not_accept_zero_packet_endpoint();
    puts("NEXUS USB BULK INTERFACE DESCRIPTOR TESTS PASSED");
    return 0;
}
