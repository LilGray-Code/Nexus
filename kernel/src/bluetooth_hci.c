#include "bluetooth_hci.h"

#include <string.h>

static void put_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint16_t get_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

bool nexus_bt_hci_parse_event(const uint8_t *packet, size_t packet_length,
                              struct nexus_bt_hci_event_view *event) {
    if (packet == NULL || event == NULL || packet_length < 3U ||
        packet[0] != NEXUS_BT_HCI_PACKET_EVENT) return false;
    if ((size_t)packet[2] != packet_length - 3U) return false;
    *event = (struct nexus_bt_hci_event_view){
        .event_code = packet[1],
        .parameters = packet + 3U,
        .parameter_length = packet[2]
    };
    return true;
}


bool nexus_bt_hci_parse_le_advertising_report(
    const struct nexus_bt_hci_event_view *event, uint8_t report_index,
    struct nexus_bt_hci_le_advertising_report *report,
    uint8_t *total_reports) {
    if (event == NULL || report == NULL || total_reports == NULL ||
        event->event_code != NEXUS_BT_HCI_EVENT_LE_META ||
        event->parameters == NULL || event->parameter_length < 2U ||
        event->parameters[0] != NEXUS_BT_HCI_LE_ADVERTISING_REPORT)
        return false;
    const uint8_t count = event->parameters[1];
    if (count == 0U || count > NEXUS_BT_HCI_MAX_LE_ADVERTISING_REPORTS)
        return false;

    size_t offset = 2U;
    bool found = false;
    struct nexus_bt_hci_le_advertising_report selected = {0};
    for (uint8_t i = 0U; i < count; ++i) {
        if ((size_t)event->parameter_length - offset < 10U) return false;
        const uint8_t event_type = event->parameters[offset];
        const uint8_t address_type = event->parameters[offset + 1U];
        const uint8_t data_length = event->parameters[offset + 8U];
        if (event_type > 4U || address_type > 3U || data_length > 31U ||
            (size_t)data_length >
                (size_t)event->parameter_length - offset - 10U)
            return false;
        if (i == report_index) {
            selected.event_type = event_type;
            selected.address_type = address_type;
            for (size_t j = 0U; j < 6U; ++j)
                selected.peer_address[j] = event->parameters[offset + 2U + j];
            selected.data = event->parameters + offset + 9U;
            selected.data_length = data_length;
            selected.rssi = (int8_t)event->parameters[
                offset + 9U + data_length];
            found = true;
        }
        offset += 10U + data_length;
    }
    if (offset != event->parameter_length) return false;
    *total_reports = count;
    if (!found) return false;
    *report = selected;
    return true;
}

bool nexus_bt_hci_parse_acl(const uint8_t *packet, size_t packet_length,
                            struct nexus_bt_hci_acl_view *acl) {
    if (packet == NULL || acl == NULL || packet_length < 5U ||
        packet[0] != NEXUS_BT_HCI_PACKET_ACL) return false;
    const uint16_t handle_flags = get_le16(packet + 1U);
    const uint16_t data_length = get_le16(packet + 3U);
    if ((size_t)data_length != packet_length - 5U ||
        (handle_flags & 0x0FFFU) == 0U) return false;

    *acl = (struct nexus_bt_hci_acl_view){
        .connection_handle = (uint16_t)(handle_flags & 0x0FFFU),
        .packet_boundary = (uint8_t)((handle_flags >> 12) & 0x03U),
        .broadcast = (uint8_t)((handle_flags >> 14) & 0x03U),
        .payload = packet + 5U,
        .payload_length = data_length
    };
    return true;
}


void nexus_bt_hci_acl_reassembler_reset(
    struct nexus_bt_hci_acl_reassembler *reassembler) {
    if (reassembler == NULL) return;
    *reassembler = (struct nexus_bt_hci_acl_reassembler){0};
}

enum nexus_bt_hci_acl_reassembly_status nexus_bt_hci_acl_reassemble(
    struct nexus_bt_hci_acl_reassembler *reassembler,
    const struct nexus_bt_hci_acl_view *fragment,
    struct nexus_bt_hci_l2cap_packet *completed) {
    if (completed != NULL)
        *completed = (struct nexus_bt_hci_l2cap_packet){0};
    if (reassembler == NULL || fragment == NULL || completed == NULL ||
        fragment->connection_handle == 0U ||
        fragment->connection_handle > 0x0FFFU ||
        fragment->payload == NULL || fragment->payload_length == 0U ||
        fragment->packet_boundary > 2U) {
        nexus_bt_hci_acl_reassembler_reset(reassembler);
        return NEXUS_BT_HCI_ACL_REASSEMBLY_INVALID;
    }

    if (fragment->packet_boundary == 0U ||
        fragment->packet_boundary == 2U) {
        /* A new start abandons an incomplete predecessor. */
        nexus_bt_hci_acl_reassembler_reset(reassembler);
        if (fragment->payload_length < 4U)
            return NEXUS_BT_HCI_ACL_REASSEMBLY_INVALID;
        const uint16_t l2cap_payload_length =
            get_le16(fragment->payload);
        const uint32_t total_length =
            (uint32_t)l2cap_payload_length + 4U;
        if (total_length > NEXUS_BT_HCI_MAX_L2CAP_PACKET ||
            fragment->payload_length > total_length)
            return NEXUS_BT_HCI_ACL_REASSEMBLY_INVALID;

        reassembler->connection_handle = fragment->connection_handle;
        reassembler->expected_length = (uint16_t)total_length;
        reassembler->accumulated_length = fragment->payload_length;
        for (uint16_t i = 0U; i < fragment->payload_length; ++i)
            reassembler->packet[i] = fragment->payload[i];
        if (reassembler->accumulated_length < reassembler->expected_length) {
            reassembler->active = true;
            return NEXUS_BT_HCI_ACL_REASSEMBLY_PENDING;
        }
    } else {
        if (!reassembler->active ||
            reassembler->accumulated_length > reassembler->expected_length ||
            fragment->packet_boundary != 1U ||
            fragment->connection_handle != reassembler->connection_handle ||
            fragment->payload_length >
                reassembler->expected_length -
                    reassembler->accumulated_length) {
            nexus_bt_hci_acl_reassembler_reset(reassembler);
            return NEXUS_BT_HCI_ACL_REASSEMBLY_INVALID;
        }
        for (uint16_t i = 0U; i < fragment->payload_length; ++i)
            reassembler->packet[reassembler->accumulated_length + i] =
                fragment->payload[i];
        reassembler->accumulated_length = (uint16_t)(
            reassembler->accumulated_length + fragment->payload_length);
        if (reassembler->accumulated_length < reassembler->expected_length)
            return NEXUS_BT_HCI_ACL_REASSEMBLY_PENDING;
    }

    if (reassembler->accumulated_length != reassembler->expected_length) {
        nexus_bt_hci_acl_reassembler_reset(reassembler);
        return NEXUS_BT_HCI_ACL_REASSEMBLY_INVALID;
    }
    completed->connection_handle = reassembler->connection_handle;
    completed->packet = reassembler->packet;
    completed->packet_length = reassembler->expected_length;
    reassembler->active = false;
    reassembler->accumulated_length = 0U;
    reassembler->expected_length = 0U;
    return NEXUS_BT_HCI_ACL_REASSEMBLY_COMPLETE;
}

bool nexus_bt_hci_connection_complete(
    const struct nexus_bt_hci_event_view *event,
    struct nexus_bt_hci_connection *connection) {
    if (event == NULL || connection == NULL || event->parameters == NULL)
        return false;
    if (event->event_code == NEXUS_BT_HCI_EVENT_CONNECTION_COMPLETE) {
        if (event->parameter_length != 11U) return false;
        if (event->parameters[0] != 0U) return false;
        const uint16_t handle = get_le16(event->parameters + 1U);
        if (handle == 0U || handle > 0x0FFFU) return false;
        *connection = (struct nexus_bt_hci_connection){
            .connected = true,
            .low_energy = false,
            .handle = handle
        };
        for (size_t i = 0U; i < 6U; ++i)
            connection->peer_address[i] = event->parameters[3U + i];
        return true;
    }
    if (event->event_code == NEXUS_BT_HCI_EVENT_LE_META) {
        if (event->parameter_length != 19U ||
            event->parameters[0] != NEXUS_BT_HCI_LE_CONNECTION_COMPLETE ||
            event->parameters[1] != 0U) return false;
        const uint16_t handle = get_le16(event->parameters + 2U);
        if (handle == 0U || handle > 0x0FFFU) return false;
        *connection = (struct nexus_bt_hci_connection){
            .connected = true,
            .low_energy = true,
            .handle = handle
        };
        /* LE connection complete uses address at parameters[5..10]. */
        for (size_t i = 0U; i < 6U; ++i)
            connection->peer_address[i] = event->parameters[6U + i];
        return true;
    }
    return false;
}

bool nexus_bt_hci_disconnection_complete(
    const struct nexus_bt_hci_event_view *event,
    uint16_t *connection_handle, uint8_t *reason) {
    if (event == NULL || connection_handle == NULL || reason == NULL ||
        event->event_code != NEXUS_BT_HCI_EVENT_DISCONNECTION_COMPLETE ||
        event->parameters == NULL || event->parameter_length != 4U ||
        event->parameters[0] != 0U) return false;
    const uint16_t handle = get_le16(event->parameters + 1U);
    if (handle == 0U || handle > 0x0FFFU) return false;
    *connection_handle = handle;
    *reason = event->parameters[3U];
    return true;
}

bool nexus_bt_hci_build_command(uint16_t opcode,
    const uint8_t *parameters, uint8_t parameter_length,
    uint8_t *packet, size_t packet_capacity, size_t *packet_length) {
    const size_t required = 4U + (size_t)parameter_length;
    if (packet == NULL || packet_length == NULL ||
        (parameter_length != 0U && parameters == NULL) ||
        packet_capacity < required) return false;
    packet[0] = NEXUS_BT_HCI_PACKET_COMMAND;
    packet[1] = (uint8_t)(opcode & 0xFFU);
    packet[2] = (uint8_t)(opcode >> 8);
    packet[3] = parameter_length;
    for (size_t i = 0U; i < parameter_length; ++i)
        packet[4U + i] = parameters[i];
    *packet_length = required;
    return true;
}

bool nexus_bt_hci_parse_command_complete(
    const struct nexus_bt_hci_event_view *event,
    struct nexus_bt_hci_command_result *result) {
    if (event == NULL || result == NULL || event->parameters == NULL ||
        event->event_code != NEXUS_BT_HCI_EVENT_COMMAND_COMPLETE ||
        event->parameter_length < 4U) return false;
    const uint16_t opcode = get_le16(event->parameters + 1U);
    if (opcode == 0U) return false;
    *result = (struct nexus_bt_hci_command_result){
        .opcode = opcode,
        .status = event->parameters[3U],
        .command_packets_allowed = event->parameters[0],
        .return_parameters = event->parameters + 4U,
        .return_parameter_length =
            (uint8_t)(event->parameter_length - 4U)
    };
    return true;
}

bool nexus_bt_hci_parse_command_status(
    const struct nexus_bt_hci_event_view *event,
    struct nexus_bt_hci_command_result *result) {
    if (event == NULL || result == NULL || event->parameters == NULL ||
        event->event_code != NEXUS_BT_HCI_EVENT_COMMAND_STATUS ||
        event->parameter_length != 4U) return false;
    const uint16_t opcode = get_le16(event->parameters + 2U);
    if (opcode == 0U) return false;
    *result = (struct nexus_bt_hci_command_result){
        .opcode = opcode,
        .status = event->parameters[0],
        .command_packets_allowed = event->parameters[1],
        .return_parameters = NULL,
        .return_parameter_length = 0U
    };
    return true;
}


bool nexus_bt_hci_host_start_le_scan(
    struct nexus_bt_hci_host *host, uint16_t interval, uint16_t window,
    bool active_scan, bool filter_duplicates) {
    if (host == NULL || host->state != NEXUS_BT_HCI_HOST_READY ||
        host->outstanding_opcode != 0U || interval < 0x0004U ||
        interval > 0x4000U || window < 0x0004U || window > interval)
        return false;
    host->le_scan_interval = interval;
    host->le_scan_window = window;
    host->le_scan_active = active_scan;
    host->le_scan_filter_duplicates = filter_duplicates;
    host->le_scan_enable = true;
    host->state = NEXUS_BT_HCI_HOST_LE_SCAN_PARAMETERS_PENDING;
    return true;
}

bool nexus_bt_hci_host_stop_le_scan(struct nexus_bt_hci_host *host) {
    if (host == NULL || host->state != NEXUS_BT_HCI_HOST_SCANNING ||
        host->outstanding_opcode != 0U) return false;
    host->le_scan_enable = false;
    host->state = NEXUS_BT_HCI_HOST_LE_SCAN_DISABLE_PENDING;
    return true;
}

bool nexus_bt_hci_build_le_create_connection(
    const struct nexus_bt_hci_le_connection_parameters *parameters,
    uint8_t *packet, size_t capacity, size_t *packet_length) {
    if (parameters == NULL ||
        parameters->scan_interval < 0x0004U ||
        parameters->scan_interval > 0x4000U ||
        parameters->scan_window < 0x0004U ||
        parameters->scan_window > parameters->scan_interval ||
        parameters->peer_address_type > 1U ||
        parameters->own_address_type > 1U ||
        parameters->connection_interval_min < 6U ||
        parameters->connection_interval_max > 3200U ||
        parameters->connection_interval_max <
            parameters->connection_interval_min ||
        parameters->latency > 499U ||
        parameters->supervision_timeout < 10U ||
        parameters->supervision_timeout > 3200U ||
        (uint32_t)parameters->supervision_timeout * 4U <=
            (uint32_t)(parameters->latency + 1U) *
            parameters->connection_interval_max ||
        parameters->minimum_connection_event_length >
            parameters->maximum_connection_event_length ||
        packet == NULL || packet_length == NULL) return false;

    uint8_t command_parameters[25] = {0};
    put_le16(command_parameters, parameters->scan_interval);
    put_le16(command_parameters + 2U, parameters->scan_window);
    command_parameters[4] = 0U;
    command_parameters[5] = parameters->peer_address_type;
    for (size_t i = 0U; i < 6U; ++i)
        command_parameters[6U + i] = parameters->peer_address[i];
    command_parameters[12] = parameters->own_address_type;
    put_le16(command_parameters + 13U,
             parameters->connection_interval_min);
    put_le16(command_parameters + 15U,
             parameters->connection_interval_max);
    put_le16(command_parameters + 17U, parameters->latency);
    put_le16(command_parameters + 19U, parameters->supervision_timeout);
    put_le16(command_parameters + 21U,
             parameters->minimum_connection_event_length);
    put_le16(command_parameters + 23U,
             parameters->maximum_connection_event_length);
    return nexus_bt_hci_build_command(
        NEXUS_BT_HCI_OPCODE_LE_CREATE_CONNECTION,
        command_parameters, sizeof(command_parameters),
        packet, capacity, packet_length);
}

void nexus_bt_hci_host_init(struct nexus_bt_hci_host *host) {
    if (host == NULL) return;
    *host = (struct nexus_bt_hci_host){
        .state = NEXUS_BT_HCI_HOST_RESET_PENDING,
        .command_credits = 1U
    };
}

bool nexus_bt_hci_host_next_command(struct nexus_bt_hci_host *host,
    uint8_t *packet, size_t capacity, size_t *packet_length) {
    if (host == NULL || packet == NULL || packet_length == NULL ||
        host->command_credits == 0U ||
        host->outstanding_opcode != 0U) return false;

    uint16_t opcode = 0U;
    switch (host->state) {
        case NEXUS_BT_HCI_HOST_RESET_PENDING:
            opcode = NEXUS_BT_HCI_OPCODE_RESET;
            host->state = NEXUS_BT_HCI_HOST_RESET_IN_FLIGHT;
            break;
        case NEXUS_BT_HCI_HOST_VERSION_PENDING:
            opcode = NEXUS_BT_HCI_OPCODE_READ_LOCAL_VERSION;
            host->state = NEXUS_BT_HCI_HOST_VERSION_IN_FLIGHT;
            break;
        case NEXUS_BT_HCI_HOST_ADDRESS_PENDING:
            opcode = NEXUS_BT_HCI_OPCODE_READ_BD_ADDR;
            host->state = NEXUS_BT_HCI_HOST_ADDRESS_IN_FLIGHT;
            break;
        case NEXUS_BT_HCI_HOST_LE_SCAN_PARAMETERS_PENDING: {
            uint8_t parameters[7] = {
                host->le_scan_active ? 1U : 0U,
                (uint8_t)host->le_scan_interval,
                (uint8_t)(host->le_scan_interval >> 8),
                (uint8_t)host->le_scan_window,
                (uint8_t)(host->le_scan_window >> 8),
                0U, 0U
            };
            opcode = NEXUS_BT_HCI_OPCODE_LE_SET_SCAN_PARAMETERS;
            if (!nexus_bt_hci_build_command(opcode, parameters,
                    sizeof(parameters), packet, capacity, packet_length)) {
                host->state = NEXUS_BT_HCI_HOST_FAILED;
                return false;
            }
            host->state = NEXUS_BT_HCI_HOST_LE_SCAN_PARAMETERS_IN_FLIGHT;
            --host->command_credits;
            host->outstanding_opcode = opcode;
            return true;
        }
        case NEXUS_BT_HCI_HOST_LE_SCAN_ENABLE_PENDING:
        case NEXUS_BT_HCI_HOST_LE_SCAN_DISABLE_PENDING: {
            uint8_t parameters[2] = {
                host->le_scan_enable ? 1U : 0U,
                host->le_scan_enable && host->le_scan_filter_duplicates
                    ? 1U : 0U
            };
            opcode = NEXUS_BT_HCI_OPCODE_LE_SET_SCAN_ENABLE;
            if (!nexus_bt_hci_build_command(opcode, parameters,
                    sizeof(parameters), packet, capacity, packet_length)) {
                host->state = NEXUS_BT_HCI_HOST_FAILED;
                return false;
            }
            host->state = host->le_scan_enable
                ? NEXUS_BT_HCI_HOST_LE_SCAN_ENABLE_IN_FLIGHT
                : NEXUS_BT_HCI_HOST_LE_SCAN_DISABLE_IN_FLIGHT;
            --host->command_credits;
            host->outstanding_opcode = opcode;
            return true;
        }
        default:
            return false;
    }

    if (!nexus_bt_hci_build_command(opcode, NULL, 0U,
            packet, capacity, packet_length)) {
        host->state = NEXUS_BT_HCI_HOST_FAILED;
        return false;
    }
    --host->command_credits;
    host->outstanding_opcode = opcode;
    return true;
}

bool nexus_bt_hci_host_handle_event(
    struct nexus_bt_hci_host *host,
    const struct nexus_bt_hci_event_view *event) {
    if (host == NULL || event == NULL ||
        host->state == NEXUS_BT_HCI_HOST_STOPPED ||
        host->state == NEXUS_BT_HCI_HOST_FAILED ||
        host->state == NEXUS_BT_HCI_HOST_READY ||
        host->state == NEXUS_BT_HCI_HOST_SCANNING) return false;

    struct nexus_bt_hci_command_result result = {0};
    if (!nexus_bt_hci_parse_command_complete(event, &result)) {
        if (event->event_code == NEXUS_BT_HCI_EVENT_COMMAND_STATUS) {
            if (!nexus_bt_hci_parse_command_status(event, &result))
                return false;
            /* Bring-up commands are synchronous; Command Status without the
             * matching Command Complete is not enough to advance the state. */
            if (result.opcode != host->outstanding_opcode ||
                result.status != 0U) {
                host->state = NEXUS_BT_HCI_HOST_FAILED;
                host->outstanding_opcode = 0U;
                return false;
            }
            host->command_credits = result.command_packets_allowed;
            return true;
        }
        return false;
    }

    if (result.opcode != host->outstanding_opcode) return false;
    host->outstanding_opcode = 0U;
    host->command_credits = result.command_packets_allowed;
    if (result.status != 0U) {
        host->state = NEXUS_BT_HCI_HOST_FAILED;
        return false;
    }

    switch (host->state) {
        case NEXUS_BT_HCI_HOST_RESET_IN_FLIGHT:
            if (result.opcode != NEXUS_BT_HCI_OPCODE_RESET) {
                host->state = NEXUS_BT_HCI_HOST_FAILED;
                return false;
            }
            host->state = NEXUS_BT_HCI_HOST_VERSION_PENDING;
            return true;

        case NEXUS_BT_HCI_HOST_VERSION_IN_FLIGHT:
            if (result.opcode != NEXUS_BT_HCI_OPCODE_READ_LOCAL_VERSION ||
                result.return_parameters == NULL ||
                result.return_parameter_length < 8U) {
                host->state = NEXUS_BT_HCI_HOST_FAILED;
                return false;
            }
            host->hci_version = result.return_parameters[0];
            host->lmp_version = result.return_parameters[3];
            host->manufacturer = get_le16(result.return_parameters + 4U);
            host->subversion = get_le16(result.return_parameters + 6U);
            host->state = NEXUS_BT_HCI_HOST_ADDRESS_PENDING;
            return true;

        case NEXUS_BT_HCI_HOST_ADDRESS_IN_FLIGHT:
            if (result.opcode != NEXUS_BT_HCI_OPCODE_READ_BD_ADDR ||
                result.return_parameters == NULL ||
                result.return_parameter_length < 6U) {
                host->state = NEXUS_BT_HCI_HOST_FAILED;
                return false;
            }
            for (size_t i = 0U; i < 6U; ++i)
                host->controller_address[i] = result.return_parameters[i];
            host->state = NEXUS_BT_HCI_HOST_READY;
            return true;

        case NEXUS_BT_HCI_HOST_LE_SCAN_PARAMETERS_IN_FLIGHT:
            if (result.opcode != NEXUS_BT_HCI_OPCODE_LE_SET_SCAN_PARAMETERS) {
                host->state = NEXUS_BT_HCI_HOST_FAILED;
                return false;
            }
            host->state = NEXUS_BT_HCI_HOST_LE_SCAN_ENABLE_PENDING;
            return true;

        case NEXUS_BT_HCI_HOST_LE_SCAN_ENABLE_IN_FLIGHT:
            if (result.opcode != NEXUS_BT_HCI_OPCODE_LE_SET_SCAN_ENABLE) {
                host->state = NEXUS_BT_HCI_HOST_FAILED;
                return false;
            }
            host->state = NEXUS_BT_HCI_HOST_SCANNING;
            return true;

        case NEXUS_BT_HCI_HOST_LE_SCAN_DISABLE_IN_FLIGHT:
            if (result.opcode != NEXUS_BT_HCI_OPCODE_LE_SET_SCAN_ENABLE) {
                host->state = NEXUS_BT_HCI_HOST_FAILED;
                return false;
            }
            host->state = NEXUS_BT_HCI_HOST_READY;
            return true;

        default:
            host->state = NEXUS_BT_HCI_HOST_FAILED;
            return false;
    }
}
