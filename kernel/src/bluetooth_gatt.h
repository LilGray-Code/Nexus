#ifndef NEXUS_BLUETOOTH_GATT_H
#define NEXUS_BLUETOOTH_GATT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_BT_GATT_UUID_PRIMARY_SERVICE 0x2800U
#define NEXUS_BT_GATT_UUID_CHARACTERISTIC 0x2803U
#define NEXUS_BT_GATT_UUID_CCCD 0x2902U
#define NEXUS_BT_GATT_UUID_REPORT_REFERENCE 0x2908U
#define NEXUS_BT_GATT_UUID_HID_INFORMATION 0x2A4AU
#define NEXUS_BT_GATT_UUID_REPORT_MAP 0x2A4BU
#define NEXUS_BT_GATT_UUID_HID_CONTROL_POINT 0x2A4CU
#define NEXUS_BT_GATT_UUID_REPORT 0x2A4DU
#define NEXUS_BT_GATT_UUID_PROTOCOL_MODE 0x2A4EU
#define NEXUS_BT_GATT_MAX_REPORTS 8U
#define NEXUS_BT_GATT_MAX_CHARACTERISTICS 16U

struct nexus_bt_gatt_hid_report_characteristic {
    uint16_t declaration_handle;
    uint16_t value_handle;
    uint8_t properties;
    uint16_t cccd_handle;
    uint16_t report_reference_handle;
    uint8_t report_id;
    uint8_t report_type;
};

struct nexus_bt_gatt_characteristic {
    uint16_t declaration_handle;
    uint16_t value_handle;
    uint8_t properties;
    uint16_t uuid;
};

struct nexus_bt_gatt_hid_service {
    uint16_t start_handle;
    uint16_t end_handle;
    uint16_t hid_information_handle;
    uint16_t report_map_handle;
    uint16_t hid_control_point_handle;
    uint16_t protocol_mode_handle;
    uint8_t characteristic_count;
    struct nexus_bt_gatt_characteristic characteristics[
        NEXUS_BT_GATT_MAX_CHARACTERISTICS];
    uint8_t report_count;
    struct nexus_bt_gatt_hid_report_characteristic reports[
        NEXUS_BT_GATT_MAX_REPORTS];
};

/* Parse ATT Read By Group Type Response (primary service discovery) and
 * ATT Read By Type Response (HID characteristic discovery). */
bool nexus_bt_gatt_find_hid_service(const uint8_t *pdu, size_t pdu_length,
    struct nexus_bt_gatt_hid_service *service);
bool nexus_bt_gatt_add_hid_characteristics(const uint8_t *pdu,
    size_t pdu_length, struct nexus_bt_gatt_hid_service *service);

/* Parse a 0x2908 Report Reference descriptor: report ID, report type. */
bool nexus_bt_gatt_parse_report_reference(const uint8_t *value,
    size_t value_length, uint8_t *report_id, uint8_t *report_type);

/* Find a CCCD (0x2902) or Report Reference (0x2908) descriptor from an
 * ATT Find Information Response for one report characteristic. */
bool nexus_bt_gatt_find_report_descriptors(const uint8_t *pdu,
    size_t pdu_length, uint16_t *cccd_handle,
    uint16_t *report_reference_handle);

/* ATT request builders for discovery, Report Map reads and notifications. */
bool nexus_bt_gatt_build_read_by_group_type(uint16_t start_handle,
    uint16_t end_handle, uint16_t uuid, uint8_t *pdu, size_t capacity,
    size_t *pdu_length);
bool nexus_bt_gatt_build_read_by_type(uint16_t start_handle,
    uint16_t end_handle, uint16_t uuid, uint8_t *pdu, size_t capacity,
    size_t *pdu_length);
bool nexus_bt_gatt_build_read_request(uint16_t handle, uint8_t *pdu,
    size_t capacity, size_t *pdu_length);
bool nexus_bt_gatt_build_read_blob_request(uint16_t handle, uint16_t offset,
    uint8_t *pdu, size_t capacity, size_t *pdu_length);
bool nexus_bt_gatt_build_find_information_request(uint16_t start_handle,
    uint16_t end_handle, uint8_t *pdu, size_t capacity, size_t *pdu_length);
bool nexus_bt_gatt_response_last_group_end(
    const uint8_t *pdu, size_t pdu_length, uint16_t *last_end_handle);
bool nexus_bt_gatt_response_last_attribute_handle(
    const uint8_t *pdu, size_t pdu_length,
    uint16_t *last_declaration_handle);
bool nexus_bt_gatt_build_write_request(uint16_t handle,
    const uint8_t *value, uint16_t value_length, uint8_t *pdu,
    size_t capacity, size_t *pdu_length);
bool nexus_bt_gatt_build_enable_notifications(uint16_t cccd_handle,
    uint8_t *pdu, size_t capacity, size_t *pdu_length);

#endif
