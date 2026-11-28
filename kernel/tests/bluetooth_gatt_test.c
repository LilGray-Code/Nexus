#include "bluetooth_gatt.h"

#include <assert.h>
#include <stdio.h>

static void test_primary_service_discovery(void) {
    const uint8_t response16[] = {
        0x11U,0x06U, 0x01U,0x00U, 0x10U,0x00U, 0x12U,0x18U
    };
    struct nexus_bt_gatt_hid_service service = {0};
    assert(nexus_bt_gatt_find_hid_service(
        response16, sizeof(response16), &service));
    assert(service.start_handle == 1U && service.end_handle == 16U);

    const uint8_t response128[] = {
        0x11U,0x14U, 0x01U,0x00U, 0x10U,0x00U,
        0xFBU,0x34U,0x9BU,0x5FU,0x80U,0x00U,0x00U,0x80U,
        0x00U,0x10U,0x00U,0x00U,0x12U,0x18U,0x00U,0x00U
    };
    assert(nexus_bt_gatt_find_hid_service(
        response128, sizeof(response128), &service));

    const uint8_t malformed[] = {0x11U,0x06U,1U};
    assert(!nexus_bt_gatt_find_hid_service(
        malformed, sizeof(malformed), &service));
}

static void test_hid_characteristics(void) {
    const uint8_t response[] = {
        0x09U,0x07U,
        0x02U,0x00U,0x02U,0x03U,0x00U,0x4AU,0x2AU,
        0x04U,0x00U,0x02U,0x05U,0x00U,0x4BU,0x2AU,
        0x06U,0x00U,0x04U,0x07U,0x00U,0x4CU,0x2AU,
        0x08U,0x00U,0x0CU,0x09U,0x00U,0x4EU,0x2AU,
        0x0AU,0x00U,0x12U,0x0BU,0x00U,0x4DU,0x2AU
    };
    struct nexus_bt_gatt_hid_service service = {
        .start_handle = 1U,
        .end_handle = 16U
    };
    assert(nexus_bt_gatt_add_hid_characteristics(
        response, sizeof(response), &service));
    assert(service.hid_information_handle == 3U);
    assert(service.report_map_handle == 5U);
    assert(service.hid_control_point_handle == 7U);
    assert(service.protocol_mode_handle == 9U);
    assert(service.report_count == 1U);
    assert(service.reports[0].declaration_handle == 10U);
    assert(service.reports[0].value_handle == 11U);
    assert(service.reports[0].properties == 0x12U);

    const uint8_t malformed[] = {0x09U,0x07U,1U};
    assert(!nexus_bt_gatt_add_hid_characteristics(
        malformed, sizeof(malformed), &service));
}

static void test_report_reference_and_descriptors(void) {
    uint8_t report_id = 0U, report_type = 0U;
    const uint8_t reference[] = {2U,1U};
    assert(nexus_bt_gatt_parse_report_reference(reference,
        sizeof(reference), &report_id, &report_type));
    assert(report_id == 2U && report_type == 1U);
    const uint8_t implicit_reference[] = {0U,1U};
    assert(nexus_bt_gatt_parse_report_reference(implicit_reference,
        sizeof(implicit_reference), &report_id, &report_type));
    assert(report_id == 0U);

    const uint8_t info[] = {
        0x05U,0x01U,
        0x0CU,0x00U,0x02U,0x29U,
        0x0DU,0x00U,0x08U,0x29U
    };
    uint16_t cccd = 0U, report_reference = 0U;
    assert(nexus_bt_gatt_find_report_descriptors(info, sizeof(info),
        &cccd, &report_reference));
    assert(cccd == 12U && report_reference == 13U);

    const uint8_t info128[] = {
        0x05U,0x02U,
        0x0CU,0x00U,
        0xFBU,0x34U,0x9BU,0x5FU,0x80U,0x00U,0x00U,0x80U,
        0x00U,0x10U,0x00U,0x00U,0x02U,0x29U,0x00U,0x00U
    };
    assert(nexus_bt_gatt_find_report_descriptors(info128,
        sizeof(info128), &cccd, &report_reference));
    assert(cccd == 12U);
}

static void test_att_request_builders(void) {
    uint8_t pdu[32] = {0U};
    size_t length = 0U;
    assert(nexus_bt_gatt_build_read_by_group_type(
        1U, 0xFFFFU, NEXUS_BT_GATT_UUID_PRIMARY_SERVICE,
        pdu, sizeof(pdu), &length));
    assert(length == 7U && pdu[0] == 0x10U);
    assert(pdu[1] == 1U && pdu[2] == 0U);
    assert(pdu[5] == 0x00U && pdu[6] == 0x28U);

    assert(nexus_bt_gatt_build_read_by_type(
        1U, 0x0010U, NEXUS_BT_GATT_UUID_CHARACTERISTIC,
        pdu, sizeof(pdu), &length));
    assert(pdu[0] == 0x08U && pdu[5] == 0x03U && pdu[6] == 0x28U);

    assert(nexus_bt_gatt_build_read_request(5U,
        pdu, sizeof(pdu), &length));
    assert(length == 3U && pdu[0] == 0x0AU && pdu[1] == 5U);

    assert(nexus_bt_gatt_build_enable_notifications(12U,
        pdu, sizeof(pdu), &length));
    assert(length == 5U && pdu[0] == 0x12U);
    assert(pdu[1] == 12U && pdu[2] == 0U);
    assert(pdu[3] == 1U && pdu[4] == 0U);

    const uint8_t output[] = {1U,2U,3U};
    assert(nexus_bt_gatt_build_write_request(7U, output,
        sizeof(output), pdu, sizeof(pdu), &length));
    assert(length == 6U && pdu[0] == 0x12U && pdu[5] == 3U);

    assert(!nexus_bt_gatt_build_read_request(0U,
        pdu, sizeof(pdu), &length));
    assert(!nexus_bt_gatt_build_enable_notifications(0U,
        pdu, sizeof(pdu), &length));
}

static void test_characteristic_inventory_and_paging_helpers(void) {
    const uint8_t response[] = {
        0x09U,0x07U,
        0x02U,0x00U,0x02U,0x03U,0x00U,0x4BU,0x2AU,
        0x05U,0x00U,0x02U,0x06U,0x00U,0x4DU,0x2AU,
        0x08U,0x00U,0x12U,0x09U,0x00U,0x4DU,0x2AU
    };
    struct nexus_bt_gatt_hid_service service = {
        .start_handle = 1U,
        .end_handle = 16U
    };
    assert(nexus_bt_gatt_add_hid_characteristics(
        response,sizeof(response),&service));
    assert(service.characteristic_count == 3U);
    assert(service.characteristics[0].declaration_handle == 2U);
    assert(service.characteristics[0].value_handle == 3U);
    assert(service.characteristics[0].properties == 2U);
    assert(service.characteristics[0].uuid == NEXUS_BT_GATT_UUID_REPORT_MAP);
    assert(service.report_map_handle == 3U);
    assert(service.report_count == 2U);
    assert(service.reports[1].properties == 0x12U);

    uint8_t pdu[16] = {0U};
    size_t length = 0U;
    assert(nexus_bt_gatt_build_read_blob_request(
        3U, 23U, pdu, sizeof(pdu), &length));
    assert(length == 5U && pdu[0] == 0x0CU);
    assert(pdu[1] == 3U && pdu[3] == 23U);
    assert(!nexus_bt_gatt_build_read_blob_request(
        0U, 0U, pdu, sizeof(pdu), &length));

    assert(nexus_bt_gatt_build_find_information_request(
        10U, 14U, pdu, sizeof(pdu), &length));
    assert(length == 5U && pdu[0] == 0x04U);
    assert(pdu[1] == 10U && pdu[3] == 14U);
    assert(!nexus_bt_gatt_build_find_information_request(
        15U, 14U, pdu, sizeof(pdu), &length));

    const uint8_t group_response[] = {
        0x11U,0x06U,1U,0U,5U,0U,0x0FU,0x18U,
        6U,0U,10U,0U,0x12U,0x18U
    };
    uint16_t last = 0U;
    assert(nexus_bt_gatt_response_last_group_end(
        group_response,sizeof(group_response),&last));
    assert(last == 10U);

    const uint8_t characteristic_response[] = {
        0x09U,0x07U,
        2U,0U,2U,3U,0U,0x4BU,0x2AU,
        5U,0U,2U,6U,0U,0x4DU,0x2AU
    };
    assert(nexus_bt_gatt_response_last_attribute_handle(
        characteristic_response,sizeof(characteristic_response),&last));
    assert(last == 5U);
}

int main(void) {
    test_primary_service_discovery();
    test_hid_characteristics();
    test_report_reference_and_descriptors();
    test_att_request_builders();
    test_characteristic_inventory_and_paging_helpers();
    puts("NEXUS BLE HID GATT DISCOVERY AND REQUEST TESTS PASSED");
    return 0;
}
