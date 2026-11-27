#include "usb_hid_interfaces.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static const uint8_t composite_config[] = {
    9,2, 59,0, 3,1,0,0x80,50,
    /* Interface 0: keyboard HID boot interface. */
    9,4, 0,0,1,3,1,1,0,
    9,0x21, 0x11,0x01,0,1,0x22,63,0,
    7,5, 0x81,3,8,0,10,
    /* Interface 1: vendor-specific non-HID interface. */
    9,4, 1,0,1,0xFF,0,0,0,
    7,5, 0x02,2,64,0,0,
    /* Interface 2: mouse HID boot interface. */
    9,4, 2,0,1,3,1,2,0,
    9,0x21, 0x11,0x01,0,1,0x22,52,0,
    7,5, 0x82,3,4,0,8
};

static void test_composite_collection(void) {
    struct nexus_usb_hid_interface_set set = {0};
    assert(nexus_usb_hid_collect_interfaces(
        composite_config, sizeof(composite_config), &set));
    assert(set.configuration_value == 1U);
    assert(set.interface_count == 2U);

    assert(set.interfaces[0].number == 0U);
    assert(set.interfaces[0].subclass == 1U);
    assert(set.interfaces[0].protocol == 1U);
    assert(set.interfaces[0].endpoint_in == 0x81U);
    assert(set.interfaces[0].max_packet == 8U);
    assert(set.interfaces[0].report_descriptor_length == 63U);

    assert(set.interfaces[1].number == 2U);
    assert(set.interfaces[1].protocol == 2U);
    assert(set.interfaces[1].endpoint_in == 0x82U);
    assert(set.interfaces[1].max_packet == 4U);
}

static void test_malformed_descriptors(void) {
    struct nexus_usb_hid_interface_set set = {0};
    assert(!nexus_usb_hid_collect_interfaces(NULL, 0U, &set));
    assert(!nexus_usb_hid_collect_interfaces(composite_config, 8U, &set));

    uint8_t malformed[sizeof(composite_config)];
    for (size_t i = 0U; i < sizeof(malformed); ++i)
        malformed[i] = composite_config[i];
    malformed[0] = 8U;
    assert(!nexus_usb_hid_collect_interfaces(malformed,
        sizeof(malformed), &set));

    for (size_t i = 0U; i < sizeof(malformed); ++i)
        malformed[i] = composite_config[i];
    malformed[9U + 9U] = 0xFFU; /* Invalid HID descriptor length. */
    assert(!nexus_usb_hid_collect_interfaces(malformed,
        sizeof(malformed), &set));
}

int main(void) {
    test_composite_collection();
    test_malformed_descriptors();
    puts("NEXUS COMPOSITE USB HID INTERFACE DISCOVERY TESTS PASSED");
    return 0;
}
