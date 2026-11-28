#include "usb_hid_interfaces.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static const uint8_t composite_config[] = {
    9,2, 75,0, 3,1,0,0x80,50,
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

    struct nexus_usb_hid_interface companion = {0};
    assert(nexus_usb_hid_find_companion_interface(
        &set, 0U, 2U, &companion));
    assert(companion.number == 2U && companion.protocol == 2U);
    assert(nexus_usb_hid_find_companion_interface(
        &set, 2U, 1U, &companion));
    assert(companion.number == 0U && companion.protocol == 1U);
    assert(!nexus_usb_hid_find_companion_interface(
        &set, 0U, 1U, &companion));
}

static void test_large_composite_hid_collection(void) {
    uint8_t configuration[9U + 10U * 25U] = {0U};
    configuration[0] = 9U;
    configuration[1] = 2U;
    const uint16_t total = (uint16_t)sizeof(configuration);
    configuration[2] = (uint8_t)total;
    configuration[3] = (uint8_t)(total >> 8);
    configuration[4] = 10U;
    configuration[5] = 1U;
    configuration[7] = 0x80U;
    configuration[8] = 50U;

    size_t offset = 9U;
    for (uint8_t i = 0U; i < 10U; ++i) {
        const uint8_t interface_descriptor[] = {
            9U,4U,i,0U,1U,3U,1U,2U,0U
        };
        const uint8_t hid_descriptor[] = {
            9U,0x21U,0x11U,0x01U,0U,1U,0x22U,52U,0U
        };
        const uint8_t endpoint_descriptor[] = {
            7U,5U,(uint8_t)(0x81U + i),3U,4U,0U,8U
        };
        for (size_t j = 0U; j < sizeof(interface_descriptor); ++j)
            configuration[offset++] = interface_descriptor[j];
        for (size_t j = 0U; j < sizeof(hid_descriptor); ++j)
            configuration[offset++] = hid_descriptor[j];
        for (size_t j = 0U; j < sizeof(endpoint_descriptor); ++j)
            configuration[offset++] = endpoint_descriptor[j];
    }
    assert(offset == sizeof(configuration));

    struct nexus_usb_hid_interface_set set = {0};
    assert(nexus_usb_hid_collect_interfaces(
        configuration, sizeof(configuration), &set));
    assert(set.interface_count == 10U);
    assert(set.interfaces[0].number == 0U);
    assert(set.interfaces[9].number == 9U);
    assert(set.interfaces[9].endpoint_in == 0x8AU);
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
    test_large_composite_hid_collection();
    test_malformed_descriptors();
    puts("NEXUS COMPOSITE USB HID INTERFACE DISCOVERY TESTS PASSED");
    return 0;
}
