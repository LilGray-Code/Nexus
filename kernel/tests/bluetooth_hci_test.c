#include "bluetooth_hci.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_event_framing(void) {
    const uint8_t packet[] = {0x04U,0x0EU,0x02U,0x01U,0x10U};
    struct nexus_bt_hci_event_view event = {0};
    assert(nexus_bt_hci_parse_event(packet, sizeof(packet), &event));
    assert(event.event_code == 0x0EU && event.parameter_length == 2U);
    assert(event.parameters[0] == 1U);
    assert(!nexus_bt_hci_parse_event(packet, sizeof(packet)-1U, &event));
    assert(!nexus_bt_hci_parse_event(packet, sizeof(packet)+1U, &event));
}

static void test_acl_framing(void) {
    const uint8_t packet[] = {0x02U,0x34U,0x12U,0x03U,0x00U,0xAAU,0xBBU,0xCCU};
    struct nexus_bt_hci_acl_view acl = {0};
    assert(nexus_bt_hci_parse_acl(packet, sizeof(packet), &acl));
    assert(acl.connection_handle == 0x0234U);
    assert(acl.packet_boundary == 1U);
    assert(acl.broadcast == 0U);
    assert(acl.payload_length == 3U);
    assert(acl.payload[2] == 0xCCU);
    assert(!nexus_bt_hci_parse_acl(packet, sizeof(packet)-1U, &acl));
}

static void test_classic_connection_and_disconnect(void) {
    const uint8_t connected_parameters[] = {
        0U,0x34U,0x02U, 1U,2U,3U,4U,5U,6U, 1U,0U
    };
    const struct nexus_bt_hci_event_view connected_event = {
        .event_code = NEXUS_BT_HCI_EVENT_CONNECTION_COMPLETE,
        .parameters = connected_parameters,
        .parameter_length = sizeof(connected_parameters)
    };
    struct nexus_bt_hci_connection connection = {0};
    assert(nexus_bt_hci_connection_complete(&connected_event, &connection));
    assert(connection.connected && !connection.low_energy);
    assert(connection.handle == 0x0234U && connection.peer_address[5] == 6U);

    const uint8_t disconnected_parameters[] = {0U,0x34U,0x02U,0x13U};
    const struct nexus_bt_hci_event_view disconnected_event = {
        .event_code = NEXUS_BT_HCI_EVENT_DISCONNECTION_COMPLETE,
        .parameters = disconnected_parameters,
        .parameter_length = sizeof(disconnected_parameters)
    };
    uint16_t handle = 0U;
    uint8_t reason = 0U;
    assert(nexus_bt_hci_disconnection_complete(
        &disconnected_event, &handle, &reason));
    assert(handle == 0x0234U && reason == 0x13U);
}

static void test_le_connection(void) {
    const uint8_t parameters[] = {
        0x01U,0x00U,0x78U,0x00U, 0x00U,
        1U,2U,3U,4U,5U,6U,
        0x18U,0x00U,0x00U,0x00U,0xC8U,0x00U,0x00U,0x00U
    };
    const struct nexus_bt_hci_event_view event = {
        .event_code = NEXUS_BT_HCI_EVENT_LE_META,
        .parameters = parameters,
        .parameter_length = sizeof(parameters)
    };
    struct nexus_bt_hci_connection connection = {0};
    assert(nexus_bt_hci_connection_complete(&event, &connection));
    assert(connection.connected && connection.low_energy);
    assert(connection.handle == 0x0078U);
    assert(connection.peer_address[0] == 2U &&
           connection.peer_address[5] == 6U);
}

int main(void) {
    test_event_framing();
    test_acl_framing();
    test_classic_connection_and_disconnect();
    test_le_connection();
    puts("NEXUS BLUETOOTH HCI FRAME AND LINK EVENT TESTS PASSED");
    return 0;
}
