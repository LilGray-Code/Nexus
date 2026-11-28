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
        0x01U,0x00U,0x78U,0x00U, 0x00U,0x00U,
        1U,2U,3U,4U,5U,6U,
        0x18U,0x00U,0x00U,0x00U,0xC8U,0x00U,0x00U
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
    assert(connection.peer_address[0] == 1U &&
           connection.peer_address[5] == 6U);
}


static void test_hci_command_build_and_responses(void) {
    uint8_t packet[16] = {0U};
    size_t packet_length = 0U;
    assert(nexus_bt_hci_build_command(NEXUS_BT_HCI_OPCODE_RESET,
        NULL, 0U, packet, sizeof(packet), &packet_length));
    assert(packet_length == 4U);
    assert(packet[0] == NEXUS_BT_HCI_PACKET_COMMAND);
    assert(packet[1] == 0x03U && packet[2] == 0x0CU && packet[3] == 0U);

    const uint8_t parameter[] = {0x01U,0x02U};
    assert(nexus_bt_hci_build_command(NEXUS_BT_HCI_OPCODE_READ_LOCAL_VERSION,
        parameter, sizeof(parameter), packet, sizeof(packet), &packet_length));
    assert(packet_length == 6U && packet[1] == 0x01U && packet[2] == 0x10U);
    assert(packet[3] == 2U && packet[5] == 2U);
    assert(!nexus_bt_hci_build_command(0x1001U, parameter, 2U,
        packet, 4U, &packet_length));
    assert(!nexus_bt_hci_build_command(0x1001U, NULL, 1U,
        packet, sizeof(packet), &packet_length));

    const uint8_t complete_parameters[] = {
        1U,0x03U,0x0CU,0U,0x09U,0x01U
    };
    const struct nexus_bt_hci_event_view complete = {
        .event_code = NEXUS_BT_HCI_EVENT_COMMAND_COMPLETE,
        .parameters = complete_parameters,
        .parameter_length = sizeof(complete_parameters)
    };
    struct nexus_bt_hci_command_result result = {0};
    assert(nexus_bt_hci_parse_command_complete(&complete, &result));
    assert(result.opcode == NEXUS_BT_HCI_OPCODE_RESET);
    assert(result.status == 0U && result.command_packets_allowed == 1U);
    assert(result.return_parameter_length == 2U);
    assert(result.return_parameters[0] == 0x09U);

    const uint8_t status_parameters[] = {0U,1U,0x01U,0x10U};
    const struct nexus_bt_hci_event_view status = {
        .event_code = NEXUS_BT_HCI_EVENT_COMMAND_STATUS,
        .parameters = status_parameters,
        .parameter_length = sizeof(status_parameters)
    };
    assert(nexus_bt_hci_parse_command_status(&status, &result));
    assert(result.opcode == NEXUS_BT_HCI_OPCODE_READ_LOCAL_VERSION);
    assert(result.status == 0U && result.command_packets_allowed == 1U);
    assert(result.return_parameters == NULL &&
           result.return_parameter_length == 0U);
}


static void test_host_initialization_state_machine(void) {
    struct nexus_bt_hci_host host = {0};
    uint8_t packet[32] = {0U};
    size_t packet_length = 0U;
    nexus_bt_hci_host_init(&host);
    assert(host.state == NEXUS_BT_HCI_HOST_RESET_PENDING);
    assert(host.command_credits == 1U);

    assert(nexus_bt_hci_host_next_command(
        &host, packet, sizeof(packet), &packet_length));
    assert(packet_length == 4U);
    assert(packet[1] == 0x03U && packet[2] == 0x0CU);
    assert(host.state == NEXUS_BT_HCI_HOST_RESET_IN_FLIGHT);
    assert(host.outstanding_opcode == NEXUS_BT_HCI_OPCODE_RESET);
    assert(!nexus_bt_hci_host_next_command(
        &host, packet, sizeof(packet), &packet_length));

    const uint8_t reset_complete_parameters[] = {
        1U,0x03U,0x0CU,0U
    };
    const struct nexus_bt_hci_event_view reset_complete = {
        .event_code = NEXUS_BT_HCI_EVENT_COMMAND_COMPLETE,
        .parameters = reset_complete_parameters,
        .parameter_length = sizeof(reset_complete_parameters)
    };
    assert(nexus_bt_hci_host_handle_event(&host, &reset_complete));
    assert(host.state == NEXUS_BT_HCI_HOST_VERSION_PENDING);
    assert(host.command_credits == 1U);

    assert(nexus_bt_hci_host_next_command(
        &host, packet, sizeof(packet), &packet_length));
    assert(packet[1] == 0x01U && packet[2] == 0x10U);
    const uint8_t version_complete_parameters[] = {
        1U,0x01U,0x10U,0U,
        0x09U,0x0BU,0x04U,0x09U,0x34U,0x12U,0x78U,0x56U
    };
    const struct nexus_bt_hci_event_view version_complete = {
        .event_code = NEXUS_BT_HCI_EVENT_COMMAND_COMPLETE,
        .parameters = version_complete_parameters,
        .parameter_length = sizeof(version_complete_parameters)
    };
    assert(nexus_bt_hci_host_handle_event(&host, &version_complete));
    assert(host.state == NEXUS_BT_HCI_HOST_ADDRESS_PENDING);
    assert(host.hci_version == 0x09U);
    assert(host.lmp_version == 0x09U);
    assert(host.manufacturer == 0x1234U);
    assert(host.subversion == 0x5678U);

    assert(nexus_bt_hci_host_next_command(
        &host, packet, sizeof(packet), &packet_length));
    assert(packet[1] == 0x09U && packet[2] == 0x10U);
    const uint8_t address_complete_parameters[] = {
        1U,0x09U,0x10U,0U, 1U,2U,3U,4U,5U,6U
    };
    const struct nexus_bt_hci_event_view address_complete = {
        .event_code = NEXUS_BT_HCI_EVENT_COMMAND_COMPLETE,
        .parameters = address_complete_parameters,
        .parameter_length = sizeof(address_complete_parameters)
    };
    assert(nexus_bt_hci_host_handle_event(&host, &address_complete));
    assert(host.state == NEXUS_BT_HCI_HOST_READY);
    assert(host.controller_address[0] == 1U);
    assert(host.controller_address[5] == 6U);
    assert(!nexus_bt_hci_host_next_command(
        &host, packet, sizeof(packet), &packet_length));
}


static void test_acl_l2cap_fragment_reassembly(void) {
    /* L2CAP length=6, CID=ATT, followed by four ATT bytes. */
    const uint8_t first_packet[] = {
        0x02U,0x34U,0x22U,0x06U,0x00U,
        0x06U,0x00U,0x04U,0x00U,0x1BU,0x20U
    };
    const uint8_t continuation_packet[] = {
        0x02U,0x34U,0x12U,0x04U,0x00U,
        0x00U,0x05U,0x06U,0x07U
    };
    struct nexus_bt_hci_acl_view first = {0};
    struct nexus_bt_hci_acl_view continuation = {0};
    struct nexus_bt_hci_acl_reassembler reassembler = {0};
    struct nexus_bt_hci_l2cap_packet complete = {0};
    assert(nexus_bt_hci_parse_acl(first_packet, sizeof(first_packet), &first));
    assert(nexus_bt_hci_parse_acl(
        continuation_packet, sizeof(continuation_packet), &continuation));
    assert(first.packet_boundary == 2U);
    assert(continuation.packet_boundary == 1U);
    assert(nexus_bt_hci_acl_reassemble(
        &reassembler, &first, &complete) ==
        NEXUS_BT_HCI_ACL_REASSEMBLY_PENDING);
    assert(reassembler.active);
    assert(nexus_bt_hci_acl_reassemble(
        &reassembler, &continuation, &complete) ==
        NEXUS_BT_HCI_ACL_REASSEMBLY_COMPLETE);
    assert(!reassembler.active);
    assert(complete.connection_handle == 0x0234U);
    assert(complete.packet_length == 10U);
    assert(complete.packet[0] == 6U && complete.packet[1] == 0U);
    assert(complete.packet[2] == 4U && complete.packet[3] == 0U);
    assert(complete.packet[4] == 0x1BU && complete.packet[9] == 7U);
}

static void test_acl_reassembly_rejects_wrong_handle_and_overrun(void) {
    const uint8_t first_packet[] = {
        0x02U,0x34U,0x22U,0x06U,0x00U,
        0x06U,0x00U,0x04U,0x00U,0xAAU,0xBBU
    };
    const uint8_t wrong_handle[] = {
        0x02U,0x35U,0x12U,0x06U,0x00U,
        0xCCU,0xDDU,0xEEU,0xFFU,0x11U,0x22U
    };
    struct nexus_bt_hci_acl_view first = {0}, wrong = {0};
    struct nexus_bt_hci_acl_reassembler reassembler = {0};
    struct nexus_bt_hci_l2cap_packet complete = {0};
    assert(nexus_bt_hci_parse_acl(first_packet, sizeof(first_packet), &first));
    assert(nexus_bt_hci_parse_acl(wrong_handle, sizeof(wrong_handle), &wrong));
    assert(nexus_bt_hci_acl_reassemble(&reassembler, &first, &complete) ==
        NEXUS_BT_HCI_ACL_REASSEMBLY_PENDING);
    assert(nexus_bt_hci_acl_reassemble(&reassembler, &wrong, &complete) ==
        NEXUS_BT_HCI_ACL_REASSEMBLY_INVALID);
    assert(!reassembler.active);

    const uint8_t overlong_first[] = {
        0x02U,0x34U,0x22U,0x07U,0x00U,
        0x02U,0x00U,0x04U,0x00U,0xAAU,0xBBU,0xCCU
    };
    struct nexus_bt_hci_acl_view overlong = {0};
    assert(nexus_bt_hci_parse_acl(
        overlong_first, sizeof(overlong_first), &overlong));
    assert(nexus_bt_hci_acl_reassemble(
        &reassembler, &overlong, &complete) ==
        NEXUS_BT_HCI_ACL_REASSEMBLY_INVALID);

    const uint8_t oversized_l2cap_header[] = {0xFD,0x03,0x04,0x00};
    const struct nexus_bt_hci_acl_view oversized = {
        .connection_handle = 0x0234U,
        .packet_boundary = 2U,
        .payload = oversized_l2cap_header,
        .payload_length = sizeof(oversized_l2cap_header)
    };
    assert(nexus_bt_hci_acl_reassemble(
        &reassembler, &oversized, &complete) ==
        NEXUS_BT_HCI_ACL_REASSEMBLY_INVALID);
}

int main(void) {
    test_event_framing();
    test_host_initialization_state_machine();
    test_hci_command_build_and_responses();
    test_acl_framing();
    test_acl_l2cap_fragment_reassembly();
    test_acl_reassembly_rejects_wrong_handle_and_overrun();
    test_classic_connection_and_disconnect();
    test_le_connection();
    puts("NEXUS BLUETOOTH HCI FRAME AND LINK EVENT TESTS PASSED");
    return 0;
}
