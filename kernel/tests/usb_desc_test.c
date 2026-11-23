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
        9,2,41,0,2,1,0,0x80,50,
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

static void test_rejects_reserved_bulk_packet_bits(void) {
    const uint8_t config[] = {
        9,2,32,0,1,1,0,0x80,50,
        9,4,0,0,2,0xFF,1,1,0,
        7,5,0x81,2,64,0x08,0,
        7,5,0x02,2,64,0,0
    };
    struct nexus_usb_bulk_interface found;
    assert(!nexus_usb_find_bulk_interface(config, sizeof(config),
        0xFF, 1, 1, &found));
}

static void test_rejects_invalid_bulk_endpoint_addresses(void) {
    const uint8_t endpoint_zero[] = {
        9,2,32,0,1,1,0,0x80,50,
        9,4,0,0,2,0xFF,1,1,0,
        7,5,0x80,2,64,0,0,
        7,5,0x02,2,64,0,0
    };
    const uint8_t reserved_address_bits[] = {
        9,2,32,0,1,1,0,0x80,50,
        9,4,0,0,2,0xFF,1,1,0,
        7,5,0x91,2,64,0,0,
        7,5,0x02,2,64,0,0
    };
    const uint8_t unsupported_packet[] = {
        9,2,32,0,1,1,0,0x80,50,
        9,4,0,0,2,0xFF,1,1,0,
        7,5,0x81,2,128,0,0,
        7,5,0x02,2,64,0,0
    };
    struct nexus_usb_bulk_interface found;
    assert(!nexus_usb_find_bulk_interface(endpoint_zero, sizeof(endpoint_zero),
        0xFF, 1, 1, &found));
    assert(!nexus_usb_find_bulk_interface(reserved_address_bits,
        sizeof(reserved_address_bits), 0xFF, 1, 1, &found));
    assert(!nexus_usb_find_bulk_interface(unsupported_packet,
        sizeof(unsupported_packet), 0xFF, 1, 1, &found));
}


static void test_rejects_interface_endpoint_count_mismatch(void) {
    const uint8_t too_many[] = {
        9,2,32,0,1,1,0,0x80,50,
        9,4,0,0,1,0xFF,1,1,0,
        7,5,0x81,2,64,0,0,
        7,5,0x02,2,64,0,0
    };
    const uint8_t too_few[] = {
        9,2,32,0,1,1,0,0x80,50,
        9,4,0,0,3,0xFF,1,1,0,
        7,5,0x81,2,64,0,0,
        7,5,0x02,2,64,0,0
    };
    struct nexus_usb_bulk_interface found;
    assert(!nexus_usb_find_bulk_interface(too_many, sizeof(too_many),
        0xFF, 1, 1, &found));
    assert(!nexus_usb_find_bulk_interface(too_few, sizeof(too_few),
        0xFF, 1, 1, &found));
}

static void test_rejects_invalid_configuration_header(void) {
    const uint8_t zero_configuration_value[] = {
        9,2,32,0,1,0,0,0x80,50,
        9,4,0,0,2,0xFF,1,1,0,
        7,5,0x81,2,64,0,0,
        7,5,0x02,2,64,0,0
    };
    const uint8_t header_longer_than_total[] = {
        10,2,9,0,1,1,0,0x80,50,0
    };
    struct nexus_usb_bulk_interface found;
    assert(!nexus_usb_find_bulk_interface(zero_configuration_value,
        sizeof(zero_configuration_value), 0xFF, 1, 1, &found));
    assert(!nexus_usb_find_bulk_interface(header_longer_than_total,
        sizeof(header_longer_than_total), 0xFF, 1, 1, &found));
}

int main(void) {
    test_bulk_pair_on_matching_interface();
    test_does_not_pair_endpoints_across_interfaces();
    test_ignores_interrupt_endpoints_and_wrong_class();
    test_rejects_malformed_descriptors();
    test_does_not_accept_zero_packet_endpoint();
    test_rejects_invalid_bulk_endpoint_addresses();
    test_rejects_reserved_bulk_packet_bits();
    test_rejects_interface_endpoint_count_mismatch();
    test_rejects_invalid_configuration_header();
    puts("NEXUS USB BULK INTERFACE DESCRIPTOR TESTS PASSED");
    return 0;
}
