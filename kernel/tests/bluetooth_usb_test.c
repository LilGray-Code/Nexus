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


struct fake_bluetooth_usb {
    uint8_t control_request_type;
    uint8_t control_request;
    uint16_t control_value;
    uint16_t control_index;
    uint8_t control_data[256];
    size_t control_length;
    uint8_t last_bulk_out_endpoint;
    uint8_t bulk_out_data[NEXUS_BLUETOOTH_USB_MAX_ACL_BYTES];
    size_t bulk_out_length;
    uint8_t event_data[NEXUS_BLUETOOTH_USB_MAX_EVENT_BYTES];
    size_t event_length;
    uint8_t acl_data[2][NEXUS_BLUETOOTH_USB_MAX_ACL_BYTES];
    size_t acl_lengths[2];
    uint8_t acl_count;
    uint8_t acl_index;
};

static bool fake_control_out(
    void *context, uint8_t request_type, uint8_t request,
    uint16_t value, uint16_t index, const uint8_t *data, size_t length) {
    struct fake_bluetooth_usb *fake = context;
    if (fake == NULL || data == NULL || length > sizeof(fake->control_data))
        return false;
    fake->control_request_type = request_type;
    fake->control_request = request;
    fake->control_value = value;
    fake->control_index = index;
    fake->control_length = length;
    for (size_t i = 0U; i < length; ++i) fake->control_data[i] = data[i];
    return true;
}

static bool fake_interrupt_in(
    void *context, uint8_t endpoint, uint8_t *data,
    size_t capacity, size_t *received) {
    struct fake_bluetooth_usb *fake = context;
    if (fake == NULL || data == NULL || received == NULL ||
        endpoint != 0x81U || fake->event_length > capacity) return false;
    for (size_t i = 0U; i < fake->event_length; ++i)
        data[i] = fake->event_data[i];
    *received = fake->event_length;
    return true;
}

static bool fake_bulk_in(
    void *context, uint8_t endpoint, uint8_t *data,
    size_t capacity, size_t *received) {
    struct fake_bluetooth_usb *fake = context;
    if (fake == NULL || data == NULL || received == NULL ||
        endpoint != 0x82U || fake->acl_index >= fake->acl_count)
        return false;
    const size_t length = fake->acl_lengths[fake->acl_index];
    if (length > capacity) return false;
    for (size_t i = 0U; i < length; ++i)
        data[i] = fake->acl_data[fake->acl_index][i];
    ++fake->acl_index;
    *received = length;
    return true;
}

static bool fake_bulk_out(
    void *context, uint8_t endpoint,
    const uint8_t *data, size_t length) {
    struct fake_bluetooth_usb *fake = context;
    if (fake == NULL || data == NULL || endpoint != 0x02U ||
        length > sizeof(fake->bulk_out_data)) return false;
    fake->last_bulk_out_endpoint = endpoint;
    fake->bulk_out_length = length;
    for (size_t i = 0U; i < length; ++i) fake->bulk_out_data[i] = data[i];
    return true;
}

static void test_usb_hci_control_command_and_event_framing(void) {
    struct nexus_bluetooth_usb_interface interface = {
        .interface_number = 3U,
        .interrupt_in = 0x81U,
        .interrupt_in_max_packet = 16U,
        .bulk_in = 0x82U,
        .bulk_in_max_packet = 64U,
        .bulk_out = 0x02U,
        .bulk_out_max_packet = 64U
    };
    struct fake_bluetooth_usb fake = {0};
    const struct nexus_bluetooth_usb_io io = {
        .context = &fake,
        .control_out = fake_control_out,
        .interrupt_in = fake_interrupt_in,
        .bulk_in = fake_bulk_in,
        .bulk_out = fake_bulk_out
    };
    struct nexus_bluetooth_usb_transport transport = {0};
    assert(nexus_bluetooth_usb_transport_init(&transport, &interface, &io));

    const uint8_t command[] = {0x01U,0x03U,0x0CU,0U};
    assert(nexus_bluetooth_usb_send_hci_command(
        &transport, command, sizeof(command)));
    assert(fake.control_request_type == 0x20U);
    assert(fake.control_request == 0U && fake.control_value == 0U);
    assert(fake.control_index == 3U);
    assert(fake.control_length == 3U);
    assert(fake.control_data[0] == 0x03U && fake.control_data[1] == 0x0CU);
    assert(!nexus_bluetooth_usb_send_hci_command(
        &transport, command, sizeof(command) - 1U));

    const uint8_t event[] = {0x0EU,0x04U,0x01U,0x03U,0x0CU,0U};
    for (size_t i = 0U; i < sizeof(event); ++i) fake.event_data[i] = event[i];
    fake.event_length = sizeof(event);
    uint8_t packet[32] = {0};
    size_t packet_length = 0U;
    assert(nexus_bluetooth_usb_read_hci_event(
        &transport, packet, sizeof(packet), &packet_length));
    assert(packet_length == sizeof(event) + 1U);
    assert(packet[0] == NEXUS_BT_HCI_PACKET_EVENT);
    struct nexus_bt_hci_event_view parsed = {0};
    assert(nexus_bt_hci_parse_event(packet, packet_length, &parsed));
    assert(parsed.event_code == NEXUS_BT_HCI_EVENT_COMMAND_COMPLETE);
}

static void test_usb_hci_acl_bulk_send_and_fragmented_receive(void) {
    struct nexus_bluetooth_usb_interface interface = {
        .interface_number = 0U,
        .interrupt_in = 0x81U,
        .interrupt_in_max_packet = 16U,
        .bulk_in = 0x82U,
        .bulk_in_max_packet = 64U,
        .bulk_out = 0x02U,
        .bulk_out_max_packet = 64U
    };
    struct fake_bluetooth_usb fake = {0};
    const struct nexus_bluetooth_usb_io io = {
        .context = &fake,
        .control_out = fake_control_out,
        .interrupt_in = fake_interrupt_in,
        .bulk_in = fake_bulk_in,
        .bulk_out = fake_bulk_out
    };
    struct nexus_bluetooth_usb_transport transport = {0};
    assert(nexus_bluetooth_usb_transport_init(&transport, &interface, &io));

    const uint8_t acl_out[] = {
        0x02U,0x34U,0x20U,0x03U,0x00U,0x01U,0x02U,0x03U
    };
    assert(nexus_bluetooth_usb_send_acl(
        &transport, acl_out, sizeof(acl_out)));
    assert(fake.last_bulk_out_endpoint == 0x02U);
    assert(fake.bulk_out_length == sizeof(acl_out) - 1U);
    assert(fake.bulk_out_data[0] == 0x34U &&
           fake.bulk_out_data[1] == 0x20U);

    const uint8_t first[] = {
        0x34U,0x22U,0x06U,0x00U,
        0x06U,0x00U,0x04U,0x00U,0x1BU,0x20U
    };
    const uint8_t continuation[] = {
        0x34U,0x12U,0x04U,0x00U,0x00U,0x05U,0x06U,0x07U
    };
    for (size_t i = 0U; i < sizeof(first); ++i)
        fake.acl_data[0][i] = first[i];
    fake.acl_lengths[0] = sizeof(first);
    for (size_t i = 0U; i < sizeof(continuation); ++i)
        fake.acl_data[1][i] = continuation[i];
    fake.acl_lengths[1] = sizeof(continuation);
    fake.acl_count = 2U;

    struct nexus_bt_hci_l2cap_packet completed = {0};
    enum nexus_bt_hci_acl_reassembly_status status;
    assert(nexus_bluetooth_usb_poll_acl(&transport, &completed, &status));
    assert(status == NEXUS_BT_HCI_ACL_REASSEMBLY_PENDING);
    assert(nexus_bluetooth_usb_poll_acl(&transport, &completed, &status));
    assert(status == NEXUS_BT_HCI_ACL_REASSEMBLY_COMPLETE);
    assert(completed.connection_handle == 0x0234U);
    assert(completed.packet_length == 10U);
    assert(completed.packet[0] == 6U && completed.packet[9] == 7U);
}


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
    test_usb_hci_control_command_and_event_framing();
    test_usb_hci_acl_bulk_send_and_fragmented_receive();
    puts("NEXUS USB BLUETOOTH HCI ENDPOINT AND PACKET TRANSPORT TESTS PASSED");
    return 0;
}
