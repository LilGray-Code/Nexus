#include "bluetooth_gatt.h"

static uint16_t get_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void put_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

bool nexus_bt_gatt_find_hid_service(const uint8_t *pdu, size_t pdu_length,
    struct nexus_bt_gatt_hid_service *service) {
    if (pdu == NULL || service == NULL || pdu_length < 2U ||
        pdu[0] != 0x11U) return false;
    const uint8_t entry_length = pdu[1];
    if ((entry_length != 6U && entry_length != 20U) ||
        pdu_length < 2U + entry_length ||
        (pdu_length - 2U) % entry_length != 0U) return false;

    for (size_t offset = 2U; offset < pdu_length; offset += entry_length) {
        const uint16_t start = get_le16(pdu + offset);
        const uint16_t end = get_le16(pdu + offset + 2U);
        if (start == 0U || end < start) return false;
        bool is_hid_service = entry_length == 6U &&
            get_le16(pdu + offset + 4U) == 0x1812U;
        if (entry_length == 20U) {
            const uint8_t *uuid = pdu + offset + 4U;
            static const uint8_t base_prefix[12] = {
                0xFBU,0x34U,0x9BU,0x5FU,0x80U,0x00U,
                0x00U,0x80U,0x00U,0x10U,0x00U,0x00U
            };
            bool base = true;
            for (size_t i = 0U; i < sizeof(base_prefix); ++i)
                if (uuid[i] != base_prefix[i]) base = false;
            if (uuid[14] != 0U || uuid[15] != 0U) base = false;
            is_hid_service = base && uuid[12] == 0x12U &&
                             uuid[13] == 0x18U;
        }
        if (is_hid_service) {
            *service = (struct nexus_bt_gatt_hid_service){
                .start_handle = start,
                .end_handle = end
            };
            return true;
        }
    }
    return false;
}

bool nexus_bt_gatt_add_hid_characteristics(const uint8_t *pdu,
    size_t pdu_length, struct nexus_bt_gatt_hid_service *service) {
    if (pdu == NULL || service == NULL || pdu_length < 2U ||
        pdu[0] != 0x09U || service->start_handle == 0U ||
        service->end_handle < service->start_handle) return false;
    const uint8_t entry_length = pdu[1];
    if ((entry_length != 7U && entry_length != 21U) ||
        pdu_length < 2U + entry_length ||
        (pdu_length - 2U) % entry_length != 0U) return false;

    for (size_t offset = 2U; offset < pdu_length; offset += entry_length) {
        const uint16_t declaration = get_le16(pdu + offset);
        const uint8_t properties = pdu[offset + 2U];
        const uint16_t value = get_le16(pdu + offset + 3U);
        if (declaration < service->start_handle ||
            declaration > service->end_handle ||
            value <= declaration || value > service->end_handle) return false;
        if (entry_length != 7U) continue;
        const uint16_t uuid = get_le16(pdu + offset + 5U);
        switch (uuid) {
            case NEXUS_BT_GATT_UUID_HID_INFORMATION:
                service->hid_information_handle = value;
                break;
            case NEXUS_BT_GATT_UUID_REPORT_MAP:
                service->report_map_handle = value;
                break;
            case NEXUS_BT_GATT_UUID_HID_CONTROL_POINT:
                service->hid_control_point_handle = value;
                break;
            case NEXUS_BT_GATT_UUID_PROTOCOL_MODE:
                service->protocol_mode_handle = value;
                break;
            case NEXUS_BT_GATT_UUID_REPORT:
                if (service->report_count >= NEXUS_BT_GATT_MAX_REPORTS)
                    return false;
                service->reports[service->report_count++] =
                    (struct nexus_bt_gatt_hid_report_characteristic){
                        .declaration_handle = declaration,
                        .value_handle = value,
                        .properties = properties
                    };
                break;
            default:
                break;
        }
    }
    return true;
}

bool nexus_bt_gatt_parse_report_reference(const uint8_t *value,
    size_t value_length, uint8_t *report_id, uint8_t *report_type) {
    if (value == NULL || report_id == NULL || report_type == NULL ||
        value_length != 2U ||
        (value[1] != 1U && value[1] != 2U && value[1] != 3U))
        return false;
    *report_id = value[0];
    *report_type = value[1];
    return true;
}

bool nexus_bt_gatt_find_report_descriptors(const uint8_t *pdu,
    size_t pdu_length, uint16_t *cccd_handle,
    uint16_t *report_reference_handle) {
    if (pdu == NULL || cccd_handle == NULL ||
        report_reference_handle == NULL || pdu_length < 2U ||
        pdu[0] != 0x05U || (pdu[1] != 1U && pdu[1] != 2U))
        return false;
    const uint8_t format = pdu[1];
    const size_t entry_length = format == 1U ? 4U : 18U;
    if (pdu_length < 2U + entry_length ||
        (pdu_length - 2U) % entry_length != 0U) return false;

    *cccd_handle = 0U;
    *report_reference_handle = 0U;
    for (size_t offset = 2U; offset < pdu_length; offset += entry_length) {
        const uint16_t handle = get_le16(pdu + offset);
        uint16_t uuid = 0U;
        if (handle == 0U) return false;
        if (format == 1U) uuid = get_le16(pdu + offset + 2U);
        else {
            /* Only the Bluetooth base UUID's 16-bit assigned-number form
             * is recognized by this bounded HID descriptor walker. */
            static const uint8_t bluetooth_base_prefix[12] = {
                0xFBU,0x34U,0x9BU,0x5FU,0x80U,0x00U,
                0x00U,0x80U,0x00U,0x10U,0x00U,0x00U
            };
            const uint8_t *uuid128 = pdu + offset + 2U;
            bool base = uuid128[14] == 0U && uuid128[15] == 0U;
            for (size_t i = 0U; i < sizeof(bluetooth_base_prefix); ++i)
                if (uuid128[i] != bluetooth_base_prefix[i]) base = false;
            if (base) uuid = (uint16_t)(uuid128[12] |
                ((uint16_t)uuid128[13] << 8));
        }
        if (uuid == NEXUS_BT_GATT_UUID_CCCD) *cccd_handle = handle;
        if (uuid == NEXUS_BT_GATT_UUID_REPORT_REFERENCE)
            *report_reference_handle = handle;
    }
    return *cccd_handle != 0U || *report_reference_handle != 0U;
}

bool nexus_bt_gatt_build_read_by_group_type(uint16_t start_handle,
    uint16_t end_handle, uint16_t uuid, uint8_t *pdu, size_t capacity,
    size_t *pdu_length) {
    if (pdu == NULL || pdu_length == NULL || start_handle == 0U ||
        end_handle < start_handle || capacity < 7U) return false;
    pdu[0] = 0x10U;
    put_le16(pdu + 1U, start_handle);
    put_le16(pdu + 3U, end_handle);
    put_le16(pdu + 5U, uuid);
    *pdu_length = 7U;
    return true;
}

bool nexus_bt_gatt_build_read_by_type(uint16_t start_handle,
    uint16_t end_handle, uint16_t uuid, uint8_t *pdu, size_t capacity,
    size_t *pdu_length) {
    if (pdu == NULL || pdu_length == NULL || start_handle == 0U ||
        end_handle < start_handle || capacity < 7U) return false;
    pdu[0] = 0x08U;
    put_le16(pdu + 1U, start_handle);
    put_le16(pdu + 3U, end_handle);
    put_le16(pdu + 5U, uuid);
    *pdu_length = 7U;
    return true;
}

bool nexus_bt_gatt_build_read_request(uint16_t handle, uint8_t *pdu,
    size_t capacity, size_t *pdu_length) {
    if (handle == 0U || pdu == NULL || pdu_length == NULL ||
        capacity < 3U) return false;
    pdu[0] = 0x0AU;
    put_le16(pdu + 1U, handle);
    *pdu_length = 3U;
    return true;
}

bool nexus_bt_gatt_build_write_request(uint16_t handle,
    const uint8_t *value, uint16_t value_length, uint8_t *pdu,
    size_t capacity, size_t *pdu_length) {
    const size_t required = 3U + (size_t)value_length;
    if (handle == 0U || value == NULL || value_length == 0U ||
        pdu == NULL || pdu_length == NULL || capacity < required)
        return false;
    pdu[0] = 0x12U;
    put_le16(pdu + 1U, handle);
    for (size_t i = 0U; i < value_length; ++i) pdu[3U + i] = value[i];
    *pdu_length = required;
    return true;
}

bool nexus_bt_gatt_build_enable_notifications(uint16_t cccd_handle,
    uint8_t *pdu, size_t capacity, size_t *pdu_length) {
    static const uint8_t notify[2] = {0x01U,0x00U};
    return nexus_bt_gatt_build_write_request(cccd_handle, notify,
        sizeof(notify), pdu, capacity, pdu_length);
}

static void nexus_bt_gatt_att_put_le16(uint8_t *pdu, uint16_t value) {
    pdu[0] = (uint8_t)value;
    pdu[1] = (uint8_t)(value >> 8);
}

static bool nexus_bt_gatt_att_valid_range(uint16_t start_handle,
                                          uint16_t end_handle) {
    return start_handle != 0U && start_handle <= end_handle;
}

bool nexus_bt_gatt_att_build_exchange_mtu(
    uint16_t client_mtu, uint8_t *pdu, size_t capacity,
    size_t *pdu_length) {
    if (pdu == NULL || pdu_length == NULL ||
        client_mtu < NEXUS_BT_ATT_DEFAULT_MTU ||
        client_mtu > NEXUS_BT_ATT_MAX_MTU || capacity < 3U) return false;
    pdu[0] = 0x02U; /* Exchange MTU Request. */
    nexus_bt_gatt_att_put_le16(pdu + 1U, client_mtu);
    *pdu_length = 3U;
    return true;
}

bool nexus_bt_gatt_att_parse_mtu_response(
    const uint8_t *pdu, size_t pdu_length,
    uint16_t client_mtu, uint16_t *negotiated_mtu) {
    if (pdu == NULL || negotiated_mtu == NULL || pdu_length != 3U ||
        pdu[0] != 0x03U ||
        client_mtu < NEXUS_BT_ATT_DEFAULT_MTU ||
        client_mtu > NEXUS_BT_ATT_MAX_MTU) return false;
    const uint16_t server_mtu =
        (uint16_t)(pdu[1] | ((uint16_t)pdu[2] << 8));
    if (server_mtu < NEXUS_BT_ATT_DEFAULT_MTU ||
        server_mtu > NEXUS_BT_ATT_MAX_MTU) return false;
    *negotiated_mtu = server_mtu < client_mtu ? server_mtu : client_mtu;
    return true;
}

bool nexus_bt_gatt_att_build_primary_service_discovery(
    uint16_t start_handle, uint16_t end_handle, uint16_t service_uuid,
    uint8_t *pdu, size_t capacity, size_t *pdu_length) {
    if (!nexus_bt_gatt_att_valid_range(start_handle, end_handle) ||
        pdu == NULL || pdu_length == NULL || capacity < 7U)
        return false;
    pdu[0] = 0x10U; /* Read By Group Type Request. */
    nexus_bt_gatt_att_put_le16(pdu + 1U, start_handle);
    nexus_bt_gatt_att_put_le16(pdu + 3U, end_handle);
    nexus_bt_gatt_att_put_le16(pdu + 5U, service_uuid);
    *pdu_length = 7U;
    return true;
}

bool nexus_bt_gatt_att_build_characteristic_discovery(
    uint16_t start_handle, uint16_t end_handle,
    uint8_t *pdu, size_t capacity, size_t *pdu_length) {
    if (!nexus_bt_gatt_att_valid_range(start_handle, end_handle) ||
        pdu == NULL || pdu_length == NULL || capacity < 7U)
        return false;
    pdu[0] = 0x08U; /* Read By Type Request. */
    nexus_bt_gatt_att_put_le16(pdu + 1U, start_handle);
    nexus_bt_gatt_att_put_le16(pdu + 3U, end_handle);
    nexus_bt_gatt_att_put_le16(pdu + 5U,
        NEXUS_BT_GATT_UUID_CHARACTERISTIC);
    *pdu_length = 7U;
    return true;
}

bool nexus_bt_gatt_att_build_descriptor_discovery(
    uint16_t start_handle, uint16_t end_handle,
    uint8_t *pdu, size_t capacity, size_t *pdu_length) {
    if (!nexus_bt_gatt_att_valid_range(start_handle, end_handle) ||
        pdu == NULL || pdu_length == NULL || capacity < 5U)
        return false;
    pdu[0] = 0x04U; /* Find Information Request. */
    nexus_bt_gatt_att_put_le16(pdu + 1U, start_handle);
    nexus_bt_gatt_att_put_le16(pdu + 3U, end_handle);
    *pdu_length = 5U;
    return true;
}

bool nexus_bt_gatt_att_build_read_request(
    uint16_t value_handle, uint8_t *pdu, size_t capacity,
    size_t *pdu_length) {
    if (value_handle == 0U || pdu == NULL || pdu_length == NULL ||
        capacity < 3U) return false;
    pdu[0] = 0x0AU; /* Read Request. */
    nexus_bt_gatt_att_put_le16(pdu + 1U, value_handle);
    *pdu_length = 3U;
    return true;
}

bool nexus_bt_gatt_att_build_enable_notifications(
    uint16_t cccd_handle, uint8_t *pdu, size_t capacity,
    size_t *pdu_length) {
    if (cccd_handle == 0U || pdu == NULL || pdu_length == NULL ||
        capacity < 5U) return false;
    pdu[0] = 0x12U; /* Write Request. */
    nexus_bt_gatt_att_put_le16(pdu + 1U, cccd_handle);
    nexus_bt_gatt_att_put_le16(pdu + 3U, 0x0001U);
    *pdu_length = 5U;
    return true;
}

