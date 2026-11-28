#include "bluetooth_hogp.h"

#include <string.h>

#include <string.h>

#define ATT_OP_ERROR_RESPONSE 0x01U
#define ATT_OP_EXCHANGE_MTU_REQUEST 0x02U
#define ATT_OP_EXCHANGE_MTU_RESPONSE 0x03U
#define ATT_OP_READ_BY_GROUP_TYPE_RESPONSE 0x11U
#define ATT_OP_READ_BY_TYPE_RESPONSE 0x09U
#define ATT_OP_READ_RESPONSE 0x0BU
#define ATT_OP_READ_BLOB_RESPONSE 0x0DU
#define ATT_OP_FIND_INFORMATION_RESPONSE 0x05U
#define ATT_OP_WRITE_RESPONSE 0x13U
#define ATT_ERROR_ATTRIBUTE_NOT_FOUND 0x0AU
#define ATT_ERROR_INVALID_OFFSET 0x07U
#define ATT_PROPERTY_NOTIFY 0x10U
#define ATT_PROPERTY_INDICATE 0x20U

static uint16_t get_le16(const uint8_t *pdu) {
    return (uint16_t)(pdu[0] | ((uint16_t)pdu[1] << 8));
}

static void fail_client(struct nexus_bt_hogp_client *client) {
    if (client == NULL) return;
    client->state = NEXUS_BT_HOGP_FAILED;
    client->awaiting_response = false;
}

bool nexus_bt_hogp_client_start(
    struct nexus_bt_hogp_client *client, uint16_t preferred_mtu) {
    if (client == NULL || preferred_mtu < 23U ||
        preferred_mtu > NEXUS_BT_HOGP_MAX_MTU) return false;
    *client = (struct nexus_bt_hogp_client){
        .state = NEXUS_BT_HOGP_MTU_PENDING,
        .client_mtu = preferred_mtu,
        .server_mtu = 23U
    };
    return true;
}

static bool set_request(
    struct nexus_bt_hogp_client *client,
    const uint8_t *request, size_t length,
    uint8_t *pdu, size_t capacity, size_t *pdu_length,
    enum nexus_bt_hogp_state wait_state) {
    if (client == NULL || request == NULL || length == 0U ||
        pdu == NULL || pdu_length == NULL || capacity < length ||
        client->awaiting_response) return false;
    for (size_t i = 0U; i < length; ++i) pdu[i] = request[i];
    *pdu_length = length;
    client->request_opcode = request[0];
    client->awaiting_response = true;
    client->state = wait_state;
    return true;
}

static bool build_simple_request(
    struct nexus_bt_hogp_client *client, uint8_t opcode,
    const uint8_t *body, size_t body_length,
    uint8_t *pdu, size_t capacity, size_t *pdu_length,
    enum nexus_bt_hogp_state wait_state) {
    uint8_t request[7U];
    if (body_length > sizeof(request) - 1U) return false;
    request[0] = opcode;
    for (size_t i = 0U; i < body_length; ++i)
        request[i + 1U] = body[i];
    return set_request(client, request, body_length + 1U,
        pdu, capacity, pdu_length, wait_state);
}

static void begin_report_descriptors(struct nexus_bt_hogp_client *client) {
    client->report_map_complete = true;
    client->report_index = 0U;
    client->state = NEXUS_BT_HOGP_REPORT_DESCRIPTORS_PENDING;
}

static bool select_report_descriptor_range(
    struct nexus_bt_hogp_client *client) {
    if (client == NULL || client->report_index >= client->service.report_count)
        return false;
    const struct nexus_bt_gatt_hid_report_characteristic *report =
        &client->service.reports[client->report_index];
    uint32_t next_declaration = (uint32_t)client->service.end_handle + 1U;
    for (uint8_t i = 0U; i < client->service.characteristic_count; ++i) {
        const uint16_t declaration =
            client->service.characteristics[i].declaration_handle;
        if (declaration > report->declaration_handle &&
            (uint32_t)declaration < next_declaration)
            next_declaration = declaration;
    }
    if (report->value_handle == 0xFFFFU ||
        report->value_handle + 1U >= next_declaration) return false;
    client->current_descriptor_start =
        (uint16_t)(report->value_handle + 1U);
    client->current_descriptor_end = (uint16_t)(next_declaration - 1U);
    return true;
}

static void advance_report(struct nexus_bt_hogp_client *client) {
    if (client == NULL) return;
    ++client->report_index;
    while (client->report_index < client->service.report_count) {
        struct nexus_bt_gatt_hid_report_characteristic *report =
            &client->service.reports[client->report_index];
        report->cccd_handle = 0U;
        report->report_reference_handle = 0U;
        report->report_id = 0U;
        report->report_type = 0U;
        if (select_report_descriptor_range(client)) {
            client->state = NEXUS_BT_HOGP_REPORT_DESCRIPTORS_PENDING;
            return;
        }
        ++client->report_index;
    }
    client->state = client->enabled_input_reports != 0U
        ? NEXUS_BT_HOGP_READY : NEXUS_BT_HOGP_FAILED;
}

static bool parse_att_error(
    const struct nexus_bt_hogp_client *client,
    const uint8_t *pdu, size_t pdu_length, uint8_t *error_code) {
    if (client == NULL || pdu == NULL || pdu_length != 5U ||
        pdu[0] != ATT_OP_ERROR_RESPONSE ||
        pdu[1] != client->request_opcode) return false;
    if (error_code != NULL) *error_code = pdu[4];
    return true;
}

bool nexus_bt_hogp_client_next_request(
    struct nexus_bt_hogp_client *client,
    uint8_t *pdu, size_t capacity, size_t *pdu_length) {
    if (client == NULL || pdu == NULL || pdu_length == NULL ||
        client->awaiting_response) return false;
    *pdu_length = 0U;

    uint8_t request[7U];
    size_t length = 0U;
    switch (client->state) {
        case NEXUS_BT_HOGP_MTU_PENDING: {
            const uint8_t body[2] = {
                (uint8_t)client->client_mtu,
                (uint8_t)(client->client_mtu >> 8)
            };
            return build_simple_request(client,
                ATT_OP_EXCHANGE_MTU_REQUEST, body, sizeof(body),
                pdu, capacity, pdu_length, NEXUS_BT_HOGP_MTU_WAIT);
        }
        case NEXUS_BT_HOGP_SERVICE_PENDING:
            if (!nexus_bt_gatt_build_read_by_group_type(
                    client->next_handle, 0xFFFFU,
                    NEXUS_BT_GATT_UUID_PRIMARY_SERVICE,
                    request, sizeof(request), &length)) {
                fail_client(client);
                return false;
            }
            return set_request(client, request, length, pdu, capacity,
                pdu_length, NEXUS_BT_HOGP_SERVICE_WAIT);
        case NEXUS_BT_HOGP_CHARACTERISTICS_PENDING:
            if (!nexus_bt_gatt_build_read_by_type(
                    client->next_handle, client->service.end_handle,
                    NEXUS_BT_GATT_UUID_CHARACTERISTIC,
                    request, sizeof(request), &length)) {
                fail_client(client);
                return false;
            }
            return set_request(client, request, length, pdu, capacity,
                pdu_length, NEXUS_BT_HOGP_CHARACTERISTICS_WAIT);
        case NEXUS_BT_HOGP_REPORT_MAP_PENDING:
            if (!nexus_bt_gatt_build_read_request(
                    client->service.report_map_handle,
                    request, sizeof(request), &length)) {
                fail_client(client);
                return false;
            }
            return set_request(client, request, length, pdu, capacity,
                pdu_length, NEXUS_BT_HOGP_REPORT_MAP_WAIT);
        case NEXUS_BT_HOGP_REPORT_MAP_BLOB_PENDING:
            if (!nexus_bt_gatt_build_read_blob_request(
                    client->service.report_map_handle,
                    client->report_map_offset,
                    request, sizeof(request), &length)) {
                fail_client(client);
                return false;
            }
            return set_request(client, request, length, pdu, capacity,
                pdu_length, NEXUS_BT_HOGP_REPORT_MAP_BLOB_WAIT);
        case NEXUS_BT_HOGP_REPORT_DESCRIPTORS_PENDING:
            if (!select_report_descriptor_range(client) ||
                !nexus_bt_gatt_build_find_information_request(
                    client->current_descriptor_start,
                    client->current_descriptor_end,
                    request, sizeof(request), &length)) {
                advance_report(client);
                return nexus_bt_hogp_client_next_request(
                    client, pdu, capacity, pdu_length);
            }
            return set_request(client, request, length, pdu, capacity,
                pdu_length, NEXUS_BT_HOGP_REPORT_DESCRIPTORS_WAIT);
        case NEXUS_BT_HOGP_REPORT_REFERENCE_PENDING: {
            const struct nexus_bt_gatt_hid_report_characteristic *report =
                &client->service.reports[client->report_index];
            if (!nexus_bt_gatt_build_read_request(
                    report->report_reference_handle,
                    request, sizeof(request), &length)) {
                advance_report(client);
                return nexus_bt_hogp_client_next_request(
                    client, pdu, capacity, pdu_length);
            }
            return set_request(client, request, length, pdu, capacity,
                pdu_length, NEXUS_BT_HOGP_REPORT_REFERENCE_WAIT);
        }
        case NEXUS_BT_HOGP_CCCD_WRITE_PENDING: {
            const struct nexus_bt_gatt_hid_report_characteristic *report =
                &client->service.reports[client->report_index];
            const uint16_t cccd_value =
                (report->properties & ATT_PROPERTY_NOTIFY) != 0U
                    ? 0x0001U : 0x0002U;
            const uint8_t value[2] = {
                (uint8_t)cccd_value, (uint8_t)(cccd_value >> 8)
            };
            if (!nexus_bt_gatt_build_write_request(
                    report->cccd_handle, value, sizeof(value),
                    request, sizeof(request), &length)) {
                fail_client(client);
                return false;
            }
            return set_request(client, request, length, pdu, capacity,
                pdu_length, NEXUS_BT_HOGP_CCCD_WRITE_WAIT);
        }
        default:
            return false;
    }
}

bool nexus_bt_hogp_client_handle_response(
    struct nexus_bt_hogp_client *client,
    const uint8_t *pdu, size_t pdu_length) {
    if (client == NULL || pdu == NULL || pdu_length == 0U ||
        !client->awaiting_response) return false;
    uint8_t error = 0U;
    const bool error_response =
        parse_att_error(client, pdu, pdu_length, &error);
    if (!error_response && pdu[0] == ATT_OP_ERROR_RESPONSE) {
        fail_client(client);
        return false;
    }

    const enum nexus_bt_hogp_state waiting_state = client->state;
    client->awaiting_response = false;
    if (error_response) {
        if (waiting_state == NEXUS_BT_HOGP_REPORT_MAP_BLOB_WAIT &&
            error == ATT_ERROR_INVALID_OFFSET) {
            begin_report_descriptors(client);
            return true;
        }
        if ((waiting_state == NEXUS_BT_HOGP_CHARACTERISTICS_WAIT ||
             waiting_state == NEXUS_BT_HOGP_REPORT_DESCRIPTORS_WAIT) &&
            error == ATT_ERROR_ATTRIBUTE_NOT_FOUND) {
            if (waiting_state == NEXUS_BT_HOGP_CHARACTERISTICS_WAIT) {
                if (client->service.report_map_handle == 0U) {
                    fail_client(client);
                    return false;
                }
                client->state = NEXUS_BT_HOGP_REPORT_MAP_PENDING;
            } else {
                advance_report(client);
            }
            return true;
        }
        fail_client(client);
        return false;
    }

    switch (waiting_state) {
        case NEXUS_BT_HOGP_MTU_WAIT:
            if (pdu[0] != ATT_OP_EXCHANGE_MTU_RESPONSE ||
                pdu_length != 3U || get_le16(pdu + 1U) < 23U) {
                fail_client(client);
                return false;
            }
            client->server_mtu = get_le16(pdu + 1U);
            if (client->server_mtu > client->client_mtu)
                client->server_mtu = client->client_mtu;
            client->next_handle = 1U;
            client->state = NEXUS_BT_HOGP_SERVICE_PENDING;
            return true;

        case NEXUS_BT_HOGP_SERVICE_WAIT: {
            if (pdu[0] != ATT_OP_READ_BY_GROUP_TYPE_RESPONSE) {
                fail_client(client);
                return false;
            }
            if (nexus_bt_gatt_find_hid_service(
                    pdu, pdu_length, &client->service)) {
                client->next_handle = client->service.start_handle;
                client->state = NEXUS_BT_HOGP_CHARACTERISTICS_PENDING;
                return true;
            }
            uint16_t last_end = 0U;
            if (!nexus_bt_gatt_response_last_group_end(
                    pdu, pdu_length, &last_end) || last_end == 0xFFFFU) {
                fail_client(client);
                return false;
            }
            client->next_handle = (uint16_t)(last_end + 1U);
            client->state = NEXUS_BT_HOGP_SERVICE_PENDING;
            return true;
        }

        case NEXUS_BT_HOGP_CHARACTERISTICS_WAIT: {
            if (pdu[0] != ATT_OP_READ_BY_TYPE_RESPONSE ||
                !nexus_bt_gatt_add_hid_characteristics(
                    pdu, pdu_length, &client->service)) {
                fail_client(client);
                return false;
            }
            uint16_t last_declaration = 0U;
            if (!nexus_bt_gatt_response_last_attribute_handle(
                    pdu, pdu_length, &last_declaration) ||
                last_declaration == 0xFFFFU ||
                last_declaration < client->characteristic_cursor) {
                fail_client(client);
                return false;
            }
            client->characteristic_cursor = last_declaration;
            if (last_declaration >= client->service.end_handle) {
                if (client->service.report_map_handle == 0U) {
                    fail_client(client);
                    return false;
                }
                client->state = NEXUS_BT_HOGP_REPORT_MAP_PENDING;
            } else {
                client->next_handle = (uint16_t)(last_declaration + 1U);
                client->state = NEXUS_BT_HOGP_CHARACTERISTICS_PENDING;
            }
            return true;
        }

        case NEXUS_BT_HOGP_REPORT_MAP_WAIT:
        case NEXUS_BT_HOGP_REPORT_MAP_BLOB_WAIT: {
            const uint8_t expected_opcode =
                waiting_state == NEXUS_BT_HOGP_REPORT_MAP_WAIT
                    ? ATT_OP_READ_RESPONSE : ATT_OP_READ_BLOB_RESPONSE;
            if (pdu[0] != expected_opcode || pdu_length < 2U ||
                pdu_length - 1U > NEXUS_BT_HOGP_MAX_REPORT_MAP -
                    client->report_map_length) {
                fail_client(client);
                return false;
            }
            for (size_t i = 1U; i < pdu_length; ++i)
                client->report_map[client->report_map_length++] = pdu[i];
            client->report_map_offset = client->report_map_length;
            if (pdu_length == (size_t)client->server_mtu - 1U) {
                client->state = NEXUS_BT_HOGP_REPORT_MAP_BLOB_PENDING;
            } else {
                begin_report_descriptors(client);
            }
            return true;
        }

        case NEXUS_BT_HOGP_REPORT_DESCRIPTORS_WAIT: {
            if (pdu[0] != ATT_OP_FIND_INFORMATION_RESPONSE) {
                fail_client(client);
                return false;
            }
            struct nexus_bt_gatt_hid_report_characteristic *report =
                &client->service.reports[client->report_index];
            if (!nexus_bt_gatt_find_report_descriptors(
                    pdu, pdu_length, &report->cccd_handle,
                    &report->report_reference_handle) ||
                report->report_reference_handle == 0U) {
                advance_report(client);
            } else {
                client->state = NEXUS_BT_HOGP_REPORT_REFERENCE_PENDING;
            }
            return true;
        }

        case NEXUS_BT_HOGP_REPORT_REFERENCE_WAIT: {
            struct nexus_bt_gatt_hid_report_characteristic *report =
                &client->service.reports[client->report_index];
            if (pdu[0] != ATT_OP_READ_RESPONSE ||
                pdu_length != 3U ||
                !nexus_bt_gatt_parse_report_reference(
                    pdu + 1U, pdu_length - 1U,
                    &report->report_id, &report->report_type)) {
                fail_client(client);
                return false;
            }
            const bool input_report = report->report_type == 1U;
            const bool notify = (report->properties &
                (ATT_PROPERTY_NOTIFY | ATT_PROPERTY_INDICATE)) != 0U;
            if (input_report && notify && report->cccd_handle != 0U) {
                client->state = NEXUS_BT_HOGP_CCCD_WRITE_PENDING;
            } else {
                advance_report(client);
            }
            return true;
        }

        case NEXUS_BT_HOGP_CCCD_WRITE_WAIT:
            if (pdu[0] != ATT_OP_WRITE_RESPONSE || pdu_length != 1U) {
                fail_client(client);
                return false;
            }
            ++client->enabled_input_reports;
            advance_report(client);
            return true;

        default:
            fail_client(client);
            return false;
    }
}

bool nexus_bt_hogp_client_is_ready(
    const struct nexus_bt_hogp_client *client) {
    return client != NULL && client->state == NEXUS_BT_HOGP_READY &&
           !client->awaiting_response;
}

bool nexus_bt_hogp_client_get_report_map(
    const struct nexus_bt_hogp_client *client,
    const uint8_t **report_map, size_t *report_map_length) {
    if (client == NULL || report_map == NULL || report_map_length == NULL ||
        client->report_map_length == 0U ||
        !client->report_map_complete) return false;
    *report_map = client->report_map;
    *report_map_length = client->report_map_length;
    return true;
}

bool nexus_bt_hogp_client_find_input_report(
    const struct nexus_bt_hogp_client *client,
    uint16_t value_handle, uint8_t *report_id) {
    if (client == NULL || report_id == NULL ||
        client->report_map_length == 0U ||
        !client->report_map_complete || value_handle == 0U)
        return false;
    for (uint8_t i = 0U; i < client->service.report_count; ++i) {
        const struct nexus_bt_gatt_hid_report_characteristic *report =
            &client->service.reports[i];
        if (report->value_handle == value_handle &&
            report->report_type == 1U && report->cccd_handle != 0U) {
            *report_id = report->report_id;
            return true;
        }
    }
    return false;
}


bool nexus_bt_hogp_client_decode_notification(
    struct nexus_bt_hogp_client *client,
    const struct nexus_bt_l2cap_view *l2cap,
    struct nexus_bt_hogp_input_report *input) {
    if (client == NULL || l2cap == NULL || input == NULL ||
        !client->report_map_complete || client->report_map_length == 0U)
        return false;

    struct nexus_bt_hid_report_view report = {0};
    if (!nexus_bt_hogp_parse_notification(l2cap, &report)) return false;
    uint8_t report_id = 0U;
    if (!nexus_bt_hogp_client_find_input_report(
            client, report.attribute_handle, &report_id)) return false;

    if (!client->input_layout_valid) {
        if (!usb_hid_generic_parse_descriptor_multi(
                client->report_map, client->report_map_length,
                &client->input_layout)) return false;
        client->input_layout_valid = true;
    }

    uint8_t formatted[256U];
    size_t formatted_length = report.report_length;
    size_t prefix = report_id != 0U ? 1U : 0U;
    if (formatted_length + prefix > sizeof(formatted)) return false;
    if (prefix != 0U) formatted[0] = report_id;
    for (size_t i = 0U; i < report.report_length; ++i)
        formatted[prefix + i] = report.report[i];
    formatted_length += prefix;

    *input = (struct nexus_bt_hogp_input_report){0};
    if (usb_hid_generic_decode_keyboard_multi(
            &client->input_layout, formatted, formatted_length,
            input->keyboard_report)) {
        input->kind = NEXUS_BT_HOGP_INPUT_KEYBOARD;
        return true;
    }
    if (usb_hid_generic_decode_mouse_multi(
            &client->input_layout, formatted, formatted_length,
            &input->mouse_event)) {
        input->kind = NEXUS_BT_HOGP_INPUT_MOUSE;
        return true;
    }
    return false;
}
