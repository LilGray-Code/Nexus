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

static void test_rejects_malformed_trailing_descriptors(void) {
    uint8_t config[sizeof(bluetooth_config) + 2U];
    memcpy(config, bluetooth_config, sizeof(bluetooth_config));
    config[2] = (uint8_t)sizeof(config);
    config[3] = 0U;
    config[sizeof(bluetooth_config)] = 1U; /* Descriptor length is invalid. */
    config[sizeof(bluetooth_config) + 1U] = 0x30U;

    struct nexus_bluetooth_usb_interface interface = {0};
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

static void test_usb_hci_event_framing(void) {
    const uint8_t event_bytes[] = {
        0x0EU,0x04U,0x01U,0x03U,0x0CU,0x00U
    };
    struct nexus_bt_hci_event_view event = {0};
    assert(nexus_bluetooth_usb_parse_hci_event(
        event_bytes, sizeof(event_bytes), &event));
    assert(event.event_code == 0x0EU);
    assert(event.parameter_length == 4U);
    assert(event.parameters[1] == 0x03U);
    assert(!nexus_bluetooth_usb_parse_hci_event(
        event_bytes, sizeof(event_bytes) - 1U, &event));
    const uint8_t truncated[] = {0x0EU,0x04U,0x01U};
    assert(!nexus_bluetooth_usb_parse_hci_event(
        truncated, sizeof(truncated), &event));
}

static void test_usb_acl_parse_and_build(void) {
    const uint8_t l2cap_pdu[] = {
        0x02U,0x00U,0x04U,0x00U,0xAAU,0xBBU
    };
    const uint8_t usb_acl[] = {
        0x34U,0x02U,0x06U,0x00U,0x02U,0x00U,
        0x04U,0x00U,0xAAU,0xBBU
    };
    struct nexus_bt_hci_acl_view acl = {0};
    assert(nexus_bluetooth_usb_parse_hci_acl(
        usb_acl, sizeof(usb_acl), &acl));
    assert(acl.connection_handle == 0x0234U);
    assert(acl.packet_boundary == 0U);
    assert(acl.payload_length == sizeof(l2cap_pdu));
    assert(acl.payload[4] == 0xAAU);
    assert(!nexus_bluetooth_usb_parse_hci_acl(
        usb_acl, sizeof(usb_acl) - 1U, &acl));

    uint8_t output[32] = {0U};
    size_t output_length = 0U;
    assert(nexus_bluetooth_usb_build_acl_packet(
        0x0234U, 0U, l2cap_pdu, sizeof(l2cap_pdu),
        output, sizeof(output), &output_length));
    assert(output_length == sizeof(usb_acl));
    assert(memcmp(output, usb_acl, sizeof(usb_acl)) == 0);

    assert(nexus_bluetooth_usb_build_acl_packet(
        0x0234U, 1U, l2cap_pdu, sizeof(l2cap_pdu),
        output, sizeof(output), &output_length));
    assert(output[0] == 0x34U && output[1] == 0x12U);
    assert(!nexus_bluetooth_usb_build_acl_packet(
        0U, 0U, l2cap_pdu, sizeof(l2cap_pdu),
        output, sizeof(output), &output_length));
    assert(!nexus_bluetooth_usb_build_acl_packet(
        0x0234U, 0U, l2cap_pdu, sizeof(l2cap_pdu) - 1U,
        output, sizeof(output), &output_length));
    assert(!nexus_bluetooth_usb_build_acl_packet(
        0x0234U, 0U, l2cap_pdu, sizeof(l2cap_pdu),
        output, 4U, &output_length));
}

int main(void) {
    test_valid_default_hci_interface();
    test_rejects_invalid_endpoints();
    test_can_skip_non_bluetooth_interfaces();
    test_rejects_malformed_trailing_descriptors();
    test_usb_hci_event_framing();
    test_usb_acl_parse_and_build();
    puts("NEXUS USB BLUETOOTH HCI ENDPOINT AND TRANSPORT TESTS PASSED");
    return 0;
}
