#include "bluetooth_usb.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t bluetooth_config[] = {
    9,2, 39,0, 1,1,0,0x80,50,
    9,4, 0,0,3,0xE0,0x01,0x01,0,
    7,5, 0x81,3,16,0,1,
    7,5, 0x82,2,64,0,0,
    7,5, 0x02,2,64,0,0
};

static void test_valid_default_hci_interface(void) {
    struct nexus_bluetooth_usb_interface interface = {0};
    assert(nexus_bluetooth_usb_find_interface(
        bluetooth_config, sizeof(bluetooth_config), &interface));
    assert(interface.interface_number == 0U);
    assert(interface.alternate_setting == 0U);
    assert(interface.interrupt_in == 0x81U);
    assert(interface.interrupt_in_max_packet == 16U);
    assert(interface.interrupt_interval == 1U);
    assert(interface.bulk_in == 0x82U);
    assert(interface.bulk_in_max_packet == 64U);
    assert(interface.bulk_out == 0x02U);
    assert(interface.bulk_out_max_packet == 64U);
    assert(!nexus_bluetooth_usb_find_interface(bluetooth_config,
        sizeof(bluetooth_config) - 1U, &interface));
}

static void test_rejects_invalid_endpoints(void) {
    uint8_t config[sizeof(bluetooth_config)];
    struct nexus_bluetooth_usb_interface interface = {0};

    memcpy(config, bluetooth_config, sizeof(config));
    config[20] = 0x80U; /* Endpoint zero is reserved for the default pipe. */
    assert(!nexus_bluetooth_usb_find_interface(
        config, sizeof(config), &interface));

    memcpy(config, bluetooth_config, sizeof(config));
    config[24] = 0U; /* Interrupt-IN endpoints require nonzero bInterval. */
    assert(!nexus_bluetooth_usb_find_interface(
        config, sizeof(config), &interface));

    memcpy(config, bluetooth_config, sizeof(config));
    config[27] = 0x81U; /* Bulk-IN must not reuse the interrupt endpoint. */
    assert(!nexus_bluetooth_usb_find_interface(
        config, sizeof(config), &interface));

    memcpy(config, bluetooth_config, sizeof(config));
    config[13] = 4U; /* Declared endpoint count must match parsed descriptors. */
    assert(!nexus_bluetooth_usb_find_interface(
        config, sizeof(config), &interface));

    memcpy(config, bluetooth_config, sizeof(config));
    config[28] = 0x01U;
    config[29] = 0x02U; /* Bulk packet size 513 is outside USB 2 bulk limits. */
    assert(!nexus_bluetooth_usb_find_interface(
        config, sizeof(config), &interface));

    memcpy(config, bluetooth_config, sizeof(config));
    config[20] = 0x91U; /* Reserved endpoint address bits must be clear. */
    assert(!nexus_bluetooth_usb_find_interface(
        config, sizeof(config), &interface));
}

static void test_can_skip_non_bluetooth_interfaces(void) {
    uint8_t config[9U + sizeof(bluetooth_config)] = {0U};
    memcpy(config, bluetooth_config, 9U);
    config[2] = (uint8_t)sizeof(config);
    config[3] = 0U;
    config[4] = 2U;
    config[5] = 1U;

    const uint8_t unrelated[] = {
        9U,4U,1U,0U,0U,3U,1U,1U,0U
    };
    memcpy(config + 9U, unrelated, sizeof(unrelated));
    memcpy(config + 9U + sizeof(unrelated),
        bluetooth_config + 9U, sizeof(bluetooth_config) - 9U);
    struct nexus_bluetooth_usb_interface interface = {0};
    assert(nexus_bluetooth_usb_find_interface(
        config, sizeof(config), &interface));
    assert(interface.interface_number == 0U);
}

int main(void) {
    test_valid_default_hci_interface();
    test_rejects_invalid_endpoints();
    test_can_skip_non_bluetooth_interfaces();
    puts("NEXUS USB BLUETOOTH HCI ENDPOINT VALIDATION TESTS PASSED");
    return 0;
}
