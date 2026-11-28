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


static void test_le_advertising_reports_parse_single_and_multiple(void) {
    const uint8_t single_parameters[] = {
        NEXUS_BT_HCI_LE_ADVERTISING_REPORT, 1U,
        0U, 1U, 1U,2U,3U,4U,5U,6U,
        3U, 0x02U,0x01U,0x06U, 0xC5U
    };
    const struct nexus_bt_hci_event_view single = {
        .event_code = NEXUS_BT_HCI_EVENT_LE_META,
        .parameters = single_parameters,
        .parameter_length = sizeof(single_parameters)
    };
    struct nexus_bt_hci_le_advertising_report report = {0};
    uint8_t count = 0U;
    assert(nexus_bt_hci_parse_le_advertising_report(
        &single, 0U, &report, &count));
    assert(count == 1U);
    assert(report.event_type == 0U && report.address_type == 1U);
    assert(report.peer_address[0] == 1U && report.peer_address[5] == 6U);
    assert(report.data_length == 3U);
    assert(report.data[0] == 0x02U && report.data[2] == 0x06U);
    assert(report.rssi == -59);
    assert(!nexus_bt_hci_parse_le_advertising_report(
        &single, 1U, &report, &count));

    const uint8_t multiple_parameters[] = {
        NEXUS_BT_HCI_LE_ADVERTISING_REPORT, 2U,
        0U,0U, 1U,1U,1U,1U,1U,1U, 1U,0xAAU, 0xC0U,
        4U,3U, 6U,5U,4U,3U,2U,1U, 2U,0xBBU,0xCCU, 0xD0U
    };
    const struct nexus_bt_hci_event_view multiple = {
        .event_code = NEXUS_BT_HCI_EVENT_LE_META,
        .parameters = multiple_parameters,
        .parameter_length = sizeof(multiple_parameters)
    };
    assert(nexus_bt_hci_parse_le_advertising_report(
        &multiple, 1U, &report, &count));
    assert(count == 2U);
    assert(report.event_type == 4U && report.address_type == 3U);
    assert(report.peer_address[0] == 6U && report.peer_address[5] == 1U);
    assert(report.data_length == 2U);
    assert(report.data[0] == 0xBBU && report.data[1] == 0xCCU);
    assert(report.rssi == -48);

    const struct nexus_bt_hci_event_view truncated = {
        .event_code = NEXUS_BT_HCI_EVENT_LE_META,
        .parameters = multiple_parameters,
        .parameter_length = (uint8_t)(sizeof(multiple_parameters) - 1U)
    };
    assert(!nexus_bt_hci_parse_le_advertising_report(
        &truncated, 0U, &report, &count));
}

static void test_le_scan_command_state_machine(void) {
    struct nexus_bt_hci_host host = {
        .state = NEXUS_BT_HCI_HOST_READY,
        .command_credits = 1U
    };
    uint8_t packet[32] = {0};
    size_t packet_length = 0U;
    assert(nexus_bt_hci_host_start_le_scan(
        &host, 0x0060U, 0x0030U, true, true));
    assert(host.state == NEXUS_BT_HCI_HOST_LE_SCAN_PARAMETERS_PENDING);
    assert(!nexus_bt_hci_host_start_le_scan(
        &host, 0x0003U, 0x0003U, false, false));

    assert(nexus_bt_hci_host_next_command(
        &host, packet, sizeof(packet), &packet_length));
    assert(packet_length == 11U);
    assert(packet[1] == 0x0BU && packet[2] == 0x20U);
    assert(packet[3] == 7U);
    assert(packet[4] == 1U);
    assert(packet[5] == 0x60U && packet[6] == 0U);
    assert(packet[7] == 0x30U && packet[8] == 0U);
    assert(packet[9] == 0U && packet[10] == 0U);

    const uint8_t scan_parameters_complete_bytes[] = {1U,0x0BU,0x20U,0U};
    const struct nexus_bt_hci_event_view scan_parameters_complete = {
        .event_code = NEXUS_BT_HCI_EVENT_COMMAND_COMPLETE,
        .parameters = scan_parameters_complete_bytes,
        .parameter_length = sizeof(scan_parameters_complete_bytes)
    };
    assert(nexus_bt_hci_host_handle_event(
        &host, &scan_parameters_complete));
    assert(host.state == NEXUS_BT_HCI_HOST_LE_SCAN_ENABLE_PENDING);

    assert(nexus_bt_hci_host_next_command(
        &host, packet, sizeof(packet), &packet_length));
    assert(packet_length == 6U);
    assert(packet[1] == 0x0CU && packet[2] == 0x20U);
    assert(packet[3] == 2U && packet[4] == 1U && packet[5] == 1U);
    const uint8_t scan_enable_complete_bytes[] = {1U,0x0CU,0x20U,0U};
    const struct nexus_bt_hci_event_view scan_enable_complete = {
        .event_code = NEXUS_BT_HCI_EVENT_COMMAND_COMPLETE,
        .parameters = scan_enable_complete_bytes,
        .parameter_length = sizeof(scan_enable_complete_bytes)
    };
    assert(nexus_bt_hci_host_handle_event(&host, &scan_enable_complete));
    assert(host.state == NEXUS_BT_HCI_HOST_SCANNING);
    assert(!nexus_bt_hci_host_next_command(
        &host, packet, sizeof(packet), &packet_length));

    assert(nexus_bt_hci_host_stop_le_scan(&host));
    assert(host.state == NEXUS_BT_HCI_HOST_LE_SCAN_DISABLE_PENDING);
    assert(nexus_bt_hci_host_next_command(
        &host, packet, sizeof(packet), &packet_length));
    assert(packet[1] == 0x0CU && packet[2] == 0x20U);
    assert(packet[4] == 0U && packet[5] == 0U);
    const struct nexus_bt_hci_event_view scan_disable_complete = {
        .event_code = NEXUS_BT_HCI_EVENT_COMMAND_COMPLETE,
        .parameters = scan_enable_complete_bytes,
        .parameter_length = sizeof(scan_enable_complete_bytes)
    };
    assert(nexus_bt_hci_host_handle_event(&host, &scan_disable_complete));
    assert(host.state == NEXUS_BT_HCI_HOST_READY);
}

static void test_le_create_connection_command_builder(void) {
    const struct nexus_bt_hci_le_connection_parameters parameters = {
        .scan_interval = 0x0060U,
        .scan_window = 0x0030U,
        .peer_address_type = 1U,
        .peer_address = {1U,2U,3U,4U,5U,6U},
        .own_address_type = 0U,
        .connection_interval_min = 0x0018U,
        .connection_interval_max = 0x0028U,
        .latency = 0U,
        .supervision_timeout = 0x012CU,
        .minimum_connection_event_length = 0U,
        .maximum_connection_event_length = 0U
    };
    uint8_t packet[40] = {0};
    size_t packet_length = 0U;
    assert(nexus_bt_hci_build_le_create_connection(
        &parameters, packet, sizeof(packet), &packet_length));
    assert(packet_length == 28U);
    assert(packet[0] == NEXUS_BT_HCI_PACKET_COMMAND);
    assert(packet[1] == 0x0DU && packet[2] == 0x20U);
    assert(packet[3] == 25U);
    assert(packet[4] == 0x60U && packet[5] == 0U);
    assert(packet[6] == 0x30U && packet[7] == 0U);
    assert(packet[8] == 0U && packet[9] == 1U);
    assert(packet[10] == 1U && packet[15] == 6U);
    assert(packet[17] == 0x18U && packet[19] == 0x28U);
    assert(packet[23] == 0x2CU && packet[24] == 0x01U);

    struct nexus_bt_hci_le_connection_parameters invalid = parameters;
    invalid.supervision_timeout = 10U;
    invalid.latency = 499U;
    assert(!nexus_bt_hci_build_le_create_connection(
        &invalid, packet, sizeof(packet), &packet_length));
    assert(!nexus_bt_hci_build_le_create_connection(
        &parameters, packet, 8U, &packet_length));
}

int main(void) {
    test_event_framing();
    test_host_initialization_state_machine();
    test_hci_command_build_and_responses();
    test_acl_framing();
    test_acl_l2cap_fragment_reassembly();
    test_acl_reassembly_rejects_wrong_handle_and_overrun();
    test_le_advertising_reports_parse_single_and_multiple();
    test_le_scan_command_state_machine();
    test_le_create_connection_command_builder();
    test_classic_connection_and_disconnect();
    test_le_connection();
    puts("NEXUS BLUETOOTH HCI FRAME AND LINK EVENT TESTS PASSED");
    return 0;
}
