#include "bluetooth_hci.h"

#include <string.h>

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

void nexus_bt_acl_reassembler_reset(
    struct nexus_bt_acl_reassembler *reassembler) {
    if (reassembler == NULL) return;
    *reassembler = (struct nexus_bt_acl_reassembler){0};
}

enum nexus_bt_acl_reassembly_result nexus_bt_acl_reassembler_feed(
    struct nexus_bt_acl_reassembler *reassembler,
    const struct nexus_bt_hci_acl_view *acl) {
    if (reassembler == NULL || acl == NULL ||
        acl->connection_handle == 0U ||
        acl->connection_handle > 0x0FFFU || acl->broadcast != 0U ||
        acl->payload == NULL) {
        nexus_bt_acl_reassembler_reset(reassembler);
        return NEXUS_BT_ACL_REJECTED;
    }

    const bool start = acl->packet_boundary == 0U ||
                       acl->packet_boundary == 2U;
    if (start) {
        nexus_bt_acl_reassembler_reset(reassembler);
        if (acl->payload_length < 4U) return NEXUS_BT_ACL_REJECTED;
        const uint16_t l2cap_payload_length = get_le16(acl->payload);
        const uint32_t expected = (uint32_t)l2cap_payload_length + 4U;
        if (expected > NEXUS_BT_ACL_MAX_L2CAP_PDU ||
            acl->payload_length > expected) return NEXUS_BT_ACL_REJECTED;

        reassembler->connection_handle = acl->connection_handle;
        reassembler->expected_length = (uint16_t)expected;
        reassembler->received_length = acl->payload_length;
        for (uint16_t i = 0U; i < acl->payload_length; ++i)
            reassembler->pdu[i] = acl->payload[i];
        reassembler->active = true;
    } else if (acl->packet_boundary == 1U) {
        if (!reassembler->active || reassembler->complete ||
            acl->connection_handle != reassembler->connection_handle ||
            acl->payload_length == 0U ||
            (uint32_t)reassembler->received_length +
                acl->payload_length > reassembler->expected_length) {
            nexus_bt_acl_reassembler_reset(reassembler);
            return NEXUS_BT_ACL_REJECTED;
        }
        for (uint16_t i = 0U; i < acl->payload_length; ++i)
            reassembler->pdu[reassembler->received_length + i] =
                acl->payload[i];
        reassembler->received_length = (uint16_t)(
            reassembler->received_length + acl->payload_length);
    } else {
        nexus_bt_acl_reassembler_reset(reassembler);
        return NEXUS_BT_ACL_REJECTED;
    }

    if (reassembler->received_length == reassembler->expected_length) {
        reassembler->complete = true;
        reassembler->active = false;
        return NEXUS_BT_ACL_COMPLETE;
    }
    return NEXUS_BT_ACL_PENDING;
}

bool nexus_bt_acl_reassembler_read(
    struct nexus_bt_acl_reassembler *reassembler,
    uint8_t *pdu, size_t capacity, size_t *pdu_length) {
    if (reassembler == NULL || pdu == NULL || pdu_length == NULL ||
        !reassembler->complete || reassembler->expected_length == 0U)
        return false;
    *pdu_length = reassembler->expected_length;
    if (capacity < reassembler->expected_length) return false;
    for (uint16_t i = 0U; i < reassembler->expected_length; ++i)
        pdu[i] = reassembler->pdu[i];
    nexus_bt_acl_reassembler_reset(reassembler);
    return true;
}

bool nexus_bt_hci_connection_complete(
    const struct nexus_bt_hci_event_view *event,
    struct nexus_bt_hci_connection *connection) {
    if (event == NULL || connection == NULL || event->parameters == NULL)
        return false;
    if (event->event_code == NEXUS_BT_HCI_EVENT_CONNECTION_COMPLETE) {
        if (event->parameter_length != 11U) return false;
        if (event->parameters[0] != 0U ||
            event->parameters[9U] != 1U || /* ACL only; reject SCO/eSCO. */
            event->parameters[10U] > 1U) return false;
        const uint16_t handle = get_le16(event->parameters + 1U);
        if (handle == 0U || handle > 0x0FFFU) return false;
        *connection = (struct nexus_bt_hci_connection){
            .connected = true,
            .low_energy = false,
            .encrypted = event->parameters[10U] == 1U,
            .handle = handle
        };
        for (size_t i = 0U; i < 6U; ++i)
            connection->peer_address[i] = event->parameters[3U + i];
        return true;
    }
    if (event->event_code == NEXUS_BT_HCI_EVENT_LE_META) {
        if (event->parameters[0] == NEXUS_BT_HCI_LE_CONNECTION_COMPLETE) {
            if (event->parameter_length != 19U ||
                event->parameters[1] != 0U ||
                event->parameters[4U] > 1U ||
                event->parameters[5U] > 1U) return false;
            const uint16_t handle = get_le16(event->parameters + 2U);
            if (handle == 0U || handle > 0x0FFFU) return false;
            *connection = (struct nexus_bt_hci_connection){
                .connected = true,
                .low_energy = true,
                .handle = handle,
                .peer_address_type = event->parameters[5U]
            };
            for (size_t i = 0U; i < 6U; ++i)
                connection->peer_address[i] = event->parameters[6U + i];
            return true;
        }
        if (event->parameters[0] ==
                NEXUS_BT_HCI_LE_ENHANCED_CONNECTION_COMPLETE) {
            /* LE Enhanced Connection Complete includes both RPAs after the
             * peer address. Address types 2/3 are identity-address forms;
             * normalize them to their public/random address class. */
            if (event->parameter_length != 31U ||
                event->parameters[1U] != 0U ||
                event->parameters[4U] > 1U ||
                event->parameters[5U] > 3U) return false;
            const uint16_t handle = get_le16(event->parameters + 2U);
            if (handle == 0U || handle > 0x0FFFU) return false;
            const uint8_t address_type = event->parameters[5U] >= 2U
                ? (uint8_t)(event->parameters[5U] - 2U)
                : event->parameters[5U];
            *connection = (struct nexus_bt_hci_connection){
                .connected = true,
                .low_energy = true,
                .handle = handle,
                .peer_address_type = address_type
            };
            for (size_t i = 0U; i < 6U; ++i)
                connection->peer_address[i] = event->parameters[6U + i];
            return true;
        }
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

bool nexus_bt_hci_parse_le_advertising_reports(
    const struct nexus_bt_hci_event_view *event,
    struct nexus_bt_hci_advertisement *reports,
    size_t report_capacity, size_t *report_count) {
    if (report_count == NULL) return false;
    *report_count = 0U;
    if (event == NULL || reports == NULL || event->parameters == NULL ||
        event->event_code != NEXUS_BT_HCI_EVENT_LE_META ||
        event->parameter_length < 2U ||
        event->parameters[0] != NEXUS_BT_HCI_LE_ADVERTISING_REPORT)
        return false;

    const uint8_t count = event->parameters[1U];
    if (count == 0U || count > NEXUS_BT_HCI_MAX_ADV_REPORTS ||
        count > report_capacity) return false;
    struct nexus_bt_hci_advertisement parsed[
        NEXUS_BT_HCI_MAX_ADV_REPORTS] = {0};
    size_t offset = 2U;
    for (uint8_t i = 0U; i < count; ++i) {
        if (offset + 9U > event->parameter_length) return false;
        const uint8_t event_type = event->parameters[offset++];
        const uint8_t address_type = event->parameters[offset++];
        if (event_type > 4U || address_type > 1U) return false;
        parsed[i].event_type = event_type;
        parsed[i].address_type = address_type;
        for (size_t j = 0U; j < 6U; ++j)
            parsed[i].address[j] = event->parameters[offset++];
        const uint8_t data_length = event->parameters[offset++];
        if (data_length > NEXUS_BT_HCI_MAX_ADV_DATA ||
            offset + data_length + 1U > event->parameter_length)
            return false;
        parsed[i].data_length = data_length;
        for (uint8_t j = 0U; j < data_length; ++j)
            parsed[i].data[j] = event->parameters[offset++];
        parsed[i].rssi = (int8_t)event->parameters[offset++];
    }
    if (offset != event->parameter_length) return false;
    for (uint8_t i = 0U; i < count; ++i) reports[i] = parsed[i];
    *report_count = count;
    return true;
}

bool nexus_bt_hci_advertisement_copy_name(
    const struct nexus_bt_hci_advertisement *advertisement,
    char *name, size_t name_capacity) {
    if (name == NULL || name_capacity == 0U) return false;
    name[0] = '\0';
    if (advertisement == NULL ||
        advertisement->data_length > NEXUS_BT_HCI_MAX_ADV_DATA) return false;

    size_t offset = 0U;
    const uint8_t *short_name = NULL;
    size_t short_name_length = 0U;
    while (offset < advertisement->data_length) {
        const uint8_t field_length = advertisement->data[offset++];
        if (field_length == 0U) break;
        if ((size_t)field_length > advertisement->data_length - offset)
            return false;
        const uint8_t type = advertisement->data[offset];
        const uint8_t *value = advertisement->data + offset + 1U;
        const size_t value_length = (size_t)field_length - 1U;
        if (type == 0x09U) {
            short_name = value;
            short_name_length = value_length;
            break;
        }
        if (type == 0x08U && short_name == NULL) {
            short_name = value;
            short_name_length = value_length;
        }
        offset += field_length;
    }
    if (short_name == NULL || short_name_length == 0U) return false;
    size_t copied = short_name_length;
    if (copied >= name_capacity) copied = name_capacity - 1U;
    for (size_t i = 0U; i < copied; ++i) {
        const uint8_t ch = short_name[i];
        name[i] = ch >= 0x20U && ch <= 0x7EU ? (char)ch : '?';
    }
    name[copied] = '\0';
    return copied != 0U;
}

static uint16_t hci_get_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

bool nexus_bt_hci_parse_inquiry_results(
    const struct nexus_bt_hci_event_view *event,
    struct nexus_bt_hci_classic_device *devices,
    size_t device_capacity, size_t *device_count) {
    if (device_count == NULL) return false;
    *device_count = 0U;
    if (event == NULL || event->parameters == NULL || devices == NULL ||
        (event->event_code != NEXUS_BT_HCI_EVENT_INQUIRY_RESULT &&
         event->event_code != NEXUS_BT_HCI_EVENT_INQUIRY_RESULT_RSSI) ||
        event->parameter_length < 1U) return false;
    const uint8_t count = event->parameters[0];
    const bool with_rssi =
        event->event_code == NEXUS_BT_HCI_EVENT_INQUIRY_RESULT_RSSI;
    const size_t stride = with_rssi ? 14U : 13U;
    if (count == 0U || count > NEXUS_BT_HCI_MAX_CLASSIC_RESULTS ||
        count > device_capacity ||
        (size_t)event->parameter_length != 1U + (size_t)count * stride)
        return false;
    const uint8_t *p = event->parameters + 1U;
    const uint8_t *addresses = p;
    const uint8_t *repetition = addresses + (size_t)count * 6U;
    const uint8_t *reserved = repetition + count;
    const uint8_t *classes = reserved + count;
    const uint8_t *offsets = classes + (size_t)count * 3U;
    const uint8_t *rssis = offsets + (size_t)count * 2U;
    struct nexus_bt_hci_classic_device parsed[
        NEXUS_BT_HCI_MAX_CLASSIC_RESULTS] = {0};
    for (uint8_t i = 0U; i < count; ++i) {
        if (repetition[i] > 2U) return false;
        for (size_t j = 0U; j < 6U; ++j)
            parsed[i].address[j] = addresses[(size_t)i * 6U + j];
        parsed[i].page_scan_repetition_mode = repetition[i];
        for (size_t j = 0U; j < 3U; ++j)
            parsed[i].class_of_device[j] = classes[(size_t)i * 3U + j];
        parsed[i].clock_offset = (uint16_t)(
            hci_get_le16(offsets + (size_t)i * 2U) & 0x7FFFU);
        parsed[i].rssi_valid = with_rssi;
        if (with_rssi) parsed[i].rssi = (int8_t)rssis[i];
    }
    for (uint8_t i = 0U; i < count; ++i) devices[i] = parsed[i];
    *device_count = count;
    return true;
}

bool nexus_bt_hci_parse_inquiry_complete(
    const struct nexus_bt_hci_event_view *event, uint8_t *status) {
    if (event == NULL || status == NULL ||
        event->event_code != NEXUS_BT_HCI_EVENT_INQUIRY_COMPLETE ||
        event->parameters == NULL || event->parameter_length != 1U)
        return false;
    *status = event->parameters[0];
    return true;
}

bool nexus_bt_hci_build_inquiry(
    uint8_t inquiry_length, uint8_t max_responses,
    uint8_t *packet, size_t packet_capacity, size_t *packet_length) {
    if (inquiry_length < 1U || inquiry_length > 0x30U) return false;
    const uint8_t parameters[5] = {
        0x33U,0x8BU,0x9EU,inquiry_length,max_responses
    };
    return nexus_bt_hci_build_command(NEXUS_BT_HCI_OPCODE_INQUIRY,
        parameters,sizeof(parameters),packet,packet_capacity,packet_length);
}

bool nexus_bt_hci_build_inquiry_cancel(
    uint8_t *packet, size_t packet_capacity, size_t *packet_length) {
    return nexus_bt_hci_build_command(
        NEXUS_BT_HCI_OPCODE_INQUIRY_CANCEL,NULL,0U,
        packet,packet_capacity,packet_length);
}

bool nexus_bt_hci_build_create_connection(
    const uint8_t address[6], uint16_t packet_type,
    uint8_t page_scan_repetition_mode, uint16_t clock_offset,
    bool allow_role_switch, uint8_t *packet, size_t packet_capacity,
    size_t *packet_length) {
    if (address == NULL || packet_type == 0U ||
        page_scan_repetition_mode > 2U) return false;
    uint8_t parameters[13] = {0U};
    for (size_t i = 0U; i < 6U; ++i) parameters[i] = address[i];
    parameters[6] = (uint8_t)packet_type;
    parameters[7] = (uint8_t)(packet_type >> 8);
    parameters[8] = page_scan_repetition_mode;
    parameters[9] = 0U;
    parameters[10] = (uint8_t)clock_offset;
    parameters[11] = (uint8_t)(clock_offset >> 8);
    parameters[12] = allow_role_switch ? 1U : 0U;
    return nexus_bt_hci_build_command(
        NEXUS_BT_HCI_OPCODE_CREATE_CONNECTION,
        parameters,sizeof(parameters),packet,packet_capacity,packet_length);
}

bool nexus_bt_hci_build_le_scan_parameters(
    uint8_t scan_type, uint16_t scan_interval, uint16_t scan_window,
    uint8_t own_address_type, uint8_t filter_policy,
    uint8_t *packet, size_t packet_capacity, size_t *packet_length) {
    if (scan_type > 1U || scan_interval < 0x0004U ||
        scan_interval > 0x4000U || scan_window < 0x0004U ||
        scan_window > scan_interval || own_address_type > 1U ||
        filter_policy > 1U) return false;
    const uint8_t parameters[7] = {
        scan_type,
        (uint8_t)scan_interval, (uint8_t)(scan_interval >> 8),
        (uint8_t)scan_window, (uint8_t)(scan_window >> 8),
        own_address_type, filter_policy
    };
    return nexus_bt_hci_build_command(
        NEXUS_BT_HCI_OPCODE_LE_SET_SCAN_PARAMETERS,
        parameters, sizeof(parameters), packet, packet_capacity,
        packet_length);
}

bool nexus_bt_hci_build_le_scan_enable(
    bool enable, bool filter_duplicates,
    uint8_t *packet, size_t packet_capacity, size_t *packet_length) {
    const uint8_t parameters[2] = {
        enable ? 1U : 0U, filter_duplicates ? 1U : 0U
    };
    return nexus_bt_hci_build_command(
        NEXUS_BT_HCI_OPCODE_LE_SET_SCAN_ENABLE,
        parameters, sizeof(parameters), packet, packet_capacity,
        packet_length);
}

static bool hci_all_bytes_equal(
    const uint8_t *bytes, size_t length, uint8_t value) {
    if (bytes == NULL) return false;
    uint8_t difference = 0U;
    for (size_t i = 0U; i < length; ++i)
        difference |= (uint8_t)(bytes[i] ^ value);
    return difference == 0U;
}

static bool hci_build_mask_command(
    uint16_t opcode, uint64_t mask,
    uint8_t *packet, size_t capacity, size_t *packet_length) {
    uint8_t parameters[8];
    for (size_t i = 0U; i < sizeof(parameters); ++i)
        parameters[i] = (uint8_t)(mask >> (i * 8U));
    return nexus_bt_hci_build_command(opcode, parameters,
        sizeof(parameters), packet, capacity, packet_length);
}

bool nexus_bt_hci_build_set_event_mask(
    uint64_t event_mask, uint8_t *packet, size_t packet_capacity,
    size_t *packet_length) {
    return hci_build_mask_command(NEXUS_BT_HCI_OPCODE_SET_EVENT_MASK,
        event_mask, packet, packet_capacity, packet_length);
}

bool nexus_bt_hci_build_le_set_event_mask(
    uint64_t le_event_mask, uint8_t *packet, size_t packet_capacity,
    size_t *packet_length) {
    return hci_build_mask_command(NEXUS_BT_HCI_OPCODE_LE_SET_EVENT_MASK,
        le_event_mask, packet, packet_capacity, packet_length);
}

bool nexus_bt_hci_build_le_read_local_p256_public_key(
    uint8_t *packet, size_t packet_capacity, size_t *packet_length) {
    return nexus_bt_hci_build_command(
        NEXUS_BT_HCI_OPCODE_LE_READ_LOCAL_P256_PUBLIC_KEY,
        NULL, 0U, packet, packet_capacity, packet_length);
}

bool nexus_bt_hci_build_le_generate_dhkey(
    const uint8_t remote_public_key[64],
    uint8_t *packet, size_t packet_capacity, size_t *packet_length) {
    if (remote_public_key == NULL) return false;
    return nexus_bt_hci_build_command(
        NEXUS_BT_HCI_OPCODE_LE_GENERATE_DHKEY,
        remote_public_key, 64U, packet, packet_capacity, packet_length);
}

bool nexus_bt_hci_parse_le_read_local_p256_public_key_complete(
    const struct nexus_bt_hci_event_view *event,
    uint8_t public_key[64]) {
    if (event == NULL || public_key == NULL ||
        event->event_code != NEXUS_BT_HCI_EVENT_LE_META ||
        event->parameters == NULL || event->parameter_length != 66U ||
        event->parameters[0] !=
            NEXUS_BT_HCI_LE_READ_LOCAL_P256_PUBLIC_KEY_COMPLETE ||
        event->parameters[1] != 0U) return false;
    const uint8_t *coordinates = event->parameters + 2U;
    if (hci_all_bytes_equal(coordinates, 64U, 0U) ||
        hci_all_bytes_equal(coordinates, 64U, 0xFFU)) return false;
    for (size_t i = 0U; i < 64U; ++i) public_key[i] = coordinates[i];
    return true;
}

bool nexus_bt_hci_parse_le_generate_dhkey_complete(
    const struct nexus_bt_hci_event_view *event,
    uint8_t dhkey[32]) {
    if (event == NULL || dhkey == NULL ||
        event->event_code != NEXUS_BT_HCI_EVENT_LE_META ||
        event->parameters == NULL || event->parameter_length != 34U ||
        event->parameters[0] !=
            NEXUS_BT_HCI_LE_GENERATE_DHKEY_COMPLETE ||
        event->parameters[1] != 0U) return false;
    const uint8_t *generated = event->parameters + 2U;
    /* Core HCI specifies an all-0xFF DHKey when the remote public point is
     * invalid, even when the command was accepted asynchronously. */
    if (hci_all_bytes_equal(generated, 32U, 0xFFU) ||
        hci_all_bytes_equal(generated, 32U, 0U)) return false;
    for (size_t i = 0U; i < 32U; ++i) dhkey[i] = generated[i];
    return true;
}

bool nexus_bt_hci_build_le_start_encryption(
    uint16_t connection_handle, const uint8_t random[8], uint16_t ediv,
    const uint8_t ltk[16], uint8_t *packet, size_t packet_capacity,
    size_t *packet_length) {
    if (connection_handle == 0U || connection_handle > 0x0FFFU ||
        random == NULL || ltk == NULL) return false;
    uint8_t parameters[28] = {0U};
    parameters[0] = (uint8_t)connection_handle;
    parameters[1] = (uint8_t)(connection_handle >> 8);
    for (size_t i = 0U; i < 8U; ++i) parameters[2U+i] = random[i];
    parameters[10] = (uint8_t)ediv;
    parameters[11] = (uint8_t)(ediv >> 8);
    for (size_t i = 0U; i < 16U; ++i) parameters[12U+i] = ltk[i];
    return nexus_bt_hci_build_command(
        NEXUS_BT_HCI_OPCODE_LE_START_ENCRYPTION,
        parameters, sizeof(parameters), packet, packet_capacity,
        packet_length);
}

bool nexus_bt_hci_parse_encryption_change(
    const struct nexus_bt_hci_event_view *event,
    uint16_t *connection_handle, uint8_t *status, bool *enabled) {
    if (event == NULL || connection_handle == NULL || status == NULL ||
        enabled == NULL || event->parameters == NULL ||
        event->event_code != NEXUS_BT_HCI_EVENT_ENCRYPTION_CHANGE ||
        event->parameter_length != 4U) return false;
    const uint16_t handle = get_le16(event->parameters + 1U);
    if (handle == 0U || handle > 0x0FFFU ||
        event->parameters[3U] > 1U) return false;
    *connection_handle = handle;
    *status = event->parameters[0U];
    *enabled = event->parameters[3U] != 0U;
    return true;
}

bool nexus_bt_hci_build_le_create_connection(
    uint16_t scan_interval, uint16_t scan_window,
    uint8_t initiator_filter_policy, uint8_t peer_address_type,
    const uint8_t peer_address[6], uint8_t own_address_type,
    uint16_t connection_interval_min, uint16_t connection_interval_max,
    uint16_t connection_latency, uint16_t supervision_timeout,
    uint16_t connection_event_length_min,
    uint16_t connection_event_length_max,
    uint8_t *packet, size_t packet_capacity, size_t *packet_length) {
    if (peer_address == NULL || scan_interval < 0x0004U ||
        scan_interval > 0x4000U || scan_window < 0x0004U ||
        scan_window > scan_interval || initiator_filter_policy > 1U ||
        peer_address_type > 1U || own_address_type > 1U ||
        connection_interval_min < 0x0006U ||
        connection_interval_max > 0x0C80U ||
        connection_interval_min > connection_interval_max ||
        connection_latency > 0x01F3U ||
        supervision_timeout < 0x000AU || supervision_timeout > 0x0C80U ||
        connection_event_length_min > connection_event_length_max ||
        (uint32_t)supervision_timeout * 8U <=
            ((uint32_t)connection_latency + 1U) *
            connection_interval_max * 2U) return false;

    uint8_t parameters[25] = {0U};
    const uint16_t values[2] = {scan_interval, scan_window};
    parameters[0] = (uint8_t)values[0];
    parameters[1] = (uint8_t)(values[0] >> 8);
    parameters[2] = (uint8_t)values[1];
    parameters[3] = (uint8_t)(values[1] >> 8);
    parameters[4] = initiator_filter_policy;
    parameters[5] = peer_address_type;
    for (size_t i = 0U; i < 6U; ++i) parameters[6U + i] = peer_address[i];
    parameters[12] = own_address_type;
    const uint16_t tail[6] = {
        connection_interval_min, connection_interval_max,
        connection_latency, supervision_timeout,
        connection_event_length_min, connection_event_length_max
    };
    for (size_t i = 0U; i < 6U; ++i) {
        parameters[13U + i * 2U] = (uint8_t)tail[i];
        parameters[14U + i * 2U] = (uint8_t)(tail[i] >> 8);
    }
    return nexus_bt_hci_build_command(
        NEXUS_BT_HCI_OPCODE_LE_CREATE_CONNECTION,
        parameters, sizeof(parameters), packet, packet_capacity,
        packet_length);
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

bool nexus_bt_hci_host_send_command(struct nexus_bt_hci_host *host,
    uint16_t opcode, const uint8_t *parameters, uint8_t parameter_length,
    uint8_t *packet, size_t capacity, size_t *packet_length) {
    if (host == NULL || host->state != NEXUS_BT_HCI_HOST_READY ||
        host->command_credits == 0U || host->outstanding_opcode != 0U ||
        opcode == 0U) return false;
    if (!nexus_bt_hci_build_command(opcode, parameters, parameter_length,
            packet, capacity, packet_length)) return false;
    --host->command_credits;
    host->outstanding_opcode = opcode;
    host->last_completed_opcode = 0U;
    host->last_command_status = 0xFFU;
    return true;
}

bool nexus_bt_hci_host_handle_event(
    struct nexus_bt_hci_host *host,
    const struct nexus_bt_hci_event_view *event) {
    if (host == NULL || event == NULL ||
        host->state == NEXUS_BT_HCI_HOST_STOPPED ||
        host->state == NEXUS_BT_HCI_HOST_FAILED) return false;
    const bool initialized = host->state == NEXUS_BT_HCI_HOST_READY;

    struct nexus_bt_hci_command_result result = {0};
    if (!nexus_bt_hci_parse_command_complete(event, &result)) {
        if (event->event_code == NEXUS_BT_HCI_EVENT_COMMAND_STATUS) {
            if (!nexus_bt_hci_parse_command_status(event, &result))
                return false;
            if (result.opcode != host->outstanding_opcode ||
                host->outstanding_opcode == 0U) return false;
            host->command_credits = result.command_packets_allowed;
            if (initialized) {
                host->outstanding_opcode = 0U;
                host->last_completed_opcode = result.opcode;
                host->last_command_status = result.status;
                return true;
            }
            /* Startup bring-up commands require a successful Command Status
             * followed by their matching Command Complete event. */
            if (result.status != 0U) {
                host->state = NEXUS_BT_HCI_HOST_FAILED;
                host->outstanding_opcode = 0U;
                return false;
            }
            return true;
        }
        return false;
    }

    if (result.opcode != host->outstanding_opcode ||
        host->outstanding_opcode == 0U) return false;
    host->outstanding_opcode = 0U;
    host->command_credits = result.command_packets_allowed;
    if (initialized) {
        host->last_completed_opcode = result.opcode;
        host->last_command_status = result.status;
        return true;
    }
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

        default:
            host->state = NEXUS_BT_HCI_HOST_FAILED;
            return false;
    }
}
