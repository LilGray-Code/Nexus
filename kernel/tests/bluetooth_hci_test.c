#include "bluetooth_hci.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static void test_hci_secure_connections_command_builders(void) {
    uint8_t packet[80] = {0U};
    size_t length = 0U;
    assert(nexus_bt_hci_build_set_event_mask(
        (1ULL<<2)|(1ULL<<4)|(1ULL<<7)|(1ULL<<13)|(1ULL<<14)|(1ULL<<61),
        packet,sizeof(packet),&length));
    assert(length == 12U && packet[1] == 0x01U &&
           packet[2] == 0x0CU && packet[3] == 8U);
    assert(packet[4] == 0x94U && packet[5] == 0x60U &&
           packet[11] == 0x20U);

    assert(nexus_bt_hci_build_le_set_event_mask(
        (1ULL<<0)|(1ULL<<1)|(1ULL<<7)|(1ULL<<8),
        packet,sizeof(packet),&length));
    assert(length == 12U && packet[1] == 0x01U &&
           packet[2] == 0x20U && packet[3] == 8U);
    assert(packet[4] == 0x83U && packet[5] == 0x01U);

    assert(nexus_bt_hci_build_le_read_local_p256_public_key(
        packet,sizeof(packet),&length));
    assert(length == 4U && packet[1] == 0x25U &&
           packet[2] == 0x20U && packet[3] == 0U);

    uint8_t remote_key[64];
    for (size_t i = 0U; i < sizeof(remote_key); ++i)
        remote_key[i] = (uint8_t)(i + 1U);
    assert(nexus_bt_hci_build_le_generate_dhkey(
        remote_key,packet,sizeof(packet),&length));
    assert(length == 68U && packet[1] == 0x26U &&
           packet[2] == 0x20U && packet[3] == 64U);
    assert(memcmp(packet+4U,remote_key,sizeof(remote_key)) == 0);
    assert(!nexus_bt_hci_build_le_generate_dhkey(
        NULL,packet,sizeof(packet),&length));
}

static void test_hci_secure_connections_complete_events(void) {
    uint8_t parameters[66] = {0x08U,0U};
    for (size_t i = 0U; i < 64U; ++i)
        parameters[2U+i] = (uint8_t)(i + 1U);
    struct nexus_bt_hci_event_view event = {
        .event_code = NEXUS_BT_HCI_EVENT_LE_META,
        .parameters = parameters,
        .parameter_length = sizeof(parameters)
    };
    uint8_t public_key[64] = {0U};
    assert(nexus_bt_hci_parse_le_read_local_p256_public_key_complete(
        &event,public_key));
    assert(memcmp(public_key,parameters+2U,64U) == 0);

    parameters[1] = 0x1FU;
    assert(!nexus_bt_hci_parse_le_read_local_p256_public_key_complete(
        &event,public_key));
    parameters[1] = 0U;
    memset(parameters+2U,0xFFU,64U);
    assert(!nexus_bt_hci_parse_le_read_local_p256_public_key_complete(
        &event,public_key));

    uint8_t dh_parameters[34] = {0x09U,0U};
    for (size_t i = 0U; i < 32U; ++i)
        dh_parameters[2U+i] = (uint8_t)(0x80U + i);
    event.parameters = dh_parameters;
    event.parameter_length = sizeof(dh_parameters);
    uint8_t dhkey[32] = {0U};
    assert(nexus_bt_hci_parse_le_generate_dhkey_complete(&event,dhkey));
    assert(memcmp(dhkey,dh_parameters+2U,32U) == 0);
    memset(dh_parameters+2U,0xFFU,32U);
    assert(!nexus_bt_hci_parse_le_generate_dhkey_complete(&event,dhkey));
    dh_parameters[0] = 0x08U;
    assert(!nexus_bt_hci_parse_le_generate_dhkey_complete(&event,dhkey));
}

static void test_le_start_encryption_command_and_event(void) {
    const uint8_t random[8] = {0U};
    const uint8_t ltk[16] = {
        0U,1U,2U,3U,4U,5U,6U,7U,8U,9U,10U,11U,12U,13U,14U,15U
    };
    uint8_t packet[40] = {0U};
    size_t length = 0U;
    assert(nexus_bt_hci_build_le_start_encryption(
        0x0123U,random,0U,ltk,packet,sizeof(packet),&length));
    assert(length == 32U);
    assert(packet[0] == NEXUS_BT_HCI_PACKET_COMMAND);
    assert(packet[1] == 0x19U && packet[2] == 0x20U && packet[3] == 28U);
    assert(packet[4] == 0x23U && packet[5] == 0x01U);
    assert(packet[16] == 0U && packet[17] == 1U && packet[31] == 15U);
    assert(!nexus_bt_hci_build_le_start_encryption(
        0U,random,0U,ltk,packet,sizeof(packet),&length));

    const uint8_t params[] = {0U,0x23U,0x01U,1U};
    const struct nexus_bt_hci_event_view event = {
        .event_code = NEXUS_BT_HCI_EVENT_ENCRYPTION_CHANGE,
        .parameters = params,
        .parameter_length = sizeof(params)
    };
    uint16_t handle = 0U;
    uint8_t status = 0xFFU;
    bool enabled = false;
    assert(nexus_bt_hci_parse_encryption_change(
        &event,&handle,&status,&enabled));
    assert(handle == 0x0123U && status == 0U && enabled);
}

static void test_le_scan_command_builders(void) {
    uint8_t packet[32] = {0U};
    size_t packet_length = 0U;
    assert(nexus_bt_hci_build_le_scan_parameters(
        0U, 0x0060U, 0x0030U, 0U, 0U,
        packet, sizeof(packet), &packet_length));
    assert(packet_length == 11U);
    assert(packet[0] == NEXUS_BT_HCI_PACKET_COMMAND);
    assert(packet[1] == 0x0BU && packet[2] == 0x20U);
    assert(packet[3] == 7U && packet[4] == 0U);
    assert(packet[5] == 0x60U && packet[6] == 0U);
    assert(packet[7] == 0x30U && packet[8] == 0U);

    assert(nexus_bt_hci_build_le_scan_enable(
        true, true, packet, sizeof(packet), &packet_length));
    assert(packet_length == 6U);
    assert(packet[1] == 0x0CU && packet[2] == 0x20U);
    assert(packet[4] == 1U && packet[5] == 1U);
    assert(!nexus_bt_hci_build_le_scan_parameters(
        0U, 0x0003U, 0x0003U, 0U, 0U,
        packet, sizeof(packet), &packet_length));
    assert(!nexus_bt_hci_build_le_scan_parameters(
        0U, 0x0010U, 0x0020U, 0U, 0U,
        packet, sizeof(packet), &packet_length));
}

static void test_le_create_connection_command_builder(void) {
    const uint8_t address[6] = {1U,2U,3U,4U,5U,6U};
    uint8_t packet[32] = {0U};
    size_t packet_length = 0U;
    assert(nexus_bt_hci_build_le_create_connection(
        0x0060U,0x0030U,0U,1U,address,0U,
        24U,40U,0U,400U,0U,0U,
        packet,sizeof(packet),&packet_length));
    assert(packet_length == 29U);
    assert(packet[1] == 0x0DU && packet[2] == 0x20U);
    assert(packet[3] == 25U);
    assert(packet[9] == 1U);
    assert(packet[10] == 1U && packet[15] == 6U);
    assert(!nexus_bt_hci_build_le_create_connection(
        0x0060U,0x0030U,0U,1U,address,0U,
        40U,24U,0U,400U,0U,0U,
        packet,sizeof(packet),&packet_length));
    assert(!nexus_bt_hci_build_le_create_connection(
        0x0060U,0x0030U,0U,1U,address,0U,
        24U,40U,0U,10U,0U,0U,
        packet,sizeof(packet),&packet_length));
}

static void test_le_advertising_report_parser(void) {
    const uint8_t parameters[] = {
        NEXUS_BT_HCI_LE_ADVERTISING_REPORT,1U,
        0U,0U,1U,2U,3U,4U,5U,6U,
        3U,0x02U,0x01U,0x06U,0xD8U
    };
    const struct nexus_bt_hci_event_view event = {
        .event_code = NEXUS_BT_HCI_EVENT_LE_META,
        .parameters = parameters,
        .parameter_length = sizeof(parameters)
    };
    struct nexus_bt_hci_advertisement reports[4] = {0};
    size_t count = 0U;
    assert(nexus_bt_hci_parse_le_advertising_reports(
        &event,reports,4U,&count));
    assert(count == 1U);
    assert(reports[0].event_type == 0U);
    assert(reports[0].address_type == 0U);
    assert(reports[0].address[0] == 1U &&
           reports[0].address[5] == 6U);
    assert(reports[0].data_length == 3U);
    assert(reports[0].data[0] == 0x02U &&
           reports[0].data[2] == 0x06U);
    assert(reports[0].rssi == -40);
    assert(!nexus_bt_hci_parse_le_advertising_reports(
        &event,reports,0U,&count));

    const uint8_t malformed[] = {
        NEXUS_BT_HCI_LE_ADVERTISING_REPORT,1U,
        0U,0U,1U,2U,3U,4U,5U,6U,
        32U
    };
    const struct nexus_bt_hci_event_view bad = {
        .event_code = NEXUS_BT_HCI_EVENT_LE_META,
        .parameters = malformed,
        .parameter_length = sizeof(malformed)
    };
    assert(!nexus_bt_hci_parse_le_advertising_reports(
        &bad,reports,4U,&count));
}

static void test_host_generic_command_status_path(void) {
    struct nexus_bt_hci_host host = {
        .state = NEXUS_BT_HCI_HOST_READY,
        .command_credits = 1U
    };
    const uint8_t parameters[25] = {0U};
    uint8_t packet[32] = {0U};
    size_t length = 0U;
    assert(nexus_bt_hci_host_send_command(&host,
        NEXUS_BT_HCI_OPCODE_LE_CREATE_CONNECTION,
        parameters, sizeof(parameters), packet, sizeof(packet), &length));
    assert(host.outstanding_opcode == NEXUS_BT_HCI_OPCODE_LE_CREATE_CONNECTION);
    const uint8_t status_parameters[] = {0U,1U,0x0DU,0x20U};
    const struct nexus_bt_hci_event_view status = {
        .event_code = NEXUS_BT_HCI_EVENT_COMMAND_STATUS,
        .parameters = status_parameters,
        .parameter_length = sizeof(status_parameters)
    };
    assert(nexus_bt_hci_host_handle_event(&host, &status));
    assert(host.command_credits == 1U);
    assert(host.outstanding_opcode == 0U);
    assert(host.last_completed_opcode == NEXUS_BT_HCI_OPCODE_LE_CREATE_CONNECTION);
    assert(host.last_command_status == 0U);
}

static void test_host_generic_command_credit_path(void) {
    struct nexus_bt_hci_host host = {
        .state = NEXUS_BT_HCI_HOST_READY,
        .command_credits = 1U
    };
    const uint8_t params[2] = {1U,1U};
    uint8_t packet[16] = {0U};
    size_t length = 0U;
    assert(nexus_bt_hci_host_send_command(&host,
        NEXUS_BT_HCI_OPCODE_LE_SET_SCAN_ENABLE,params,sizeof(params),
        packet,sizeof(packet),&length));
    assert(length == 6U);
    assert(host.command_credits == 0U);
    assert(host.outstanding_opcode == NEXUS_BT_HCI_OPCODE_LE_SET_SCAN_ENABLE);

    const uint8_t complete_parameters[] = {
        1U,0x0CU,0x20U,0U
    };
    const struct nexus_bt_hci_event_view complete = {
        .event_code = NEXUS_BT_HCI_EVENT_COMMAND_COMPLETE,
        .parameters = complete_parameters,
        .parameter_length = sizeof(complete_parameters)
    };
    assert(nexus_bt_hci_host_handle_event(&host,&complete));
    assert(host.command_credits == 1U);
    assert(host.outstanding_opcode == 0U);
    assert(host.last_completed_opcode == NEXUS_BT_HCI_OPCODE_LE_SET_SCAN_ENABLE);
    assert(host.last_command_status == 0U);
}

int main(void) {
    test_event_framing();
    test_host_initialization_state_machine();
    test_hci_command_build_and_responses();
    test_acl_framing();
    test_classic_connection_and_disconnect();
    test_le_connection();
    test_le_scan_command_builders();
    test_le_create_connection_command_builder();
    test_le_start_encryption_command_and_event();
    test_hci_secure_connections_command_builders();
    test_hci_secure_connections_complete_events();
    test_le_advertising_report_parser();
    test_host_generic_command_credit_path();
    test_host_generic_command_status_path();
    puts("NEXUS BLUETOOTH HCI DISCOVERY, CONNECTION AND COMMAND CREDIT TESTS PASSED");
    return 0;
}
