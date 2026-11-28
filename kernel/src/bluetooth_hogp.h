#ifndef NEXUS_BLUETOOTH_HOGP_H
#define NEXUS_BLUETOOTH_HOGP_H

#include "bluetooth_gatt.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_BT_HOGP_MAX_MTU 247U
#define NEXUS_BT_HOGP_MAX_REPORT_MAP 512U

enum nexus_bt_hogp_state {
    NEXUS_BT_HOGP_IDLE = 0U,
    NEXUS_BT_HOGP_MTU_PENDING,
    NEXUS_BT_HOGP_MTU_WAIT,
    NEXUS_BT_HOGP_SERVICE_PENDING,
    NEXUS_BT_HOGP_SERVICE_WAIT,
    NEXUS_BT_HOGP_CHARACTERISTICS_PENDING,
    NEXUS_BT_HOGP_CHARACTERISTICS_WAIT,
    NEXUS_BT_HOGP_REPORT_MAP_PENDING,
    NEXUS_BT_HOGP_REPORT_MAP_WAIT,
    NEXUS_BT_HOGP_REPORT_MAP_BLOB_PENDING,
    NEXUS_BT_HOGP_REPORT_MAP_BLOB_WAIT,
    NEXUS_BT_HOGP_REPORT_DESCRIPTORS_PENDING,
    NEXUS_BT_HOGP_REPORT_DESCRIPTORS_WAIT,
    NEXUS_BT_HOGP_REPORT_REFERENCE_PENDING,
    NEXUS_BT_HOGP_REPORT_REFERENCE_WAIT,
    NEXUS_BT_HOGP_CCCD_WRITE_PENDING,
    NEXUS_BT_HOGP_CCCD_WRITE_WAIT,
    NEXUS_BT_HOGP_READY,
    NEXUS_BT_HOGP_FAILED
};

struct nexus_bt_hogp_client {
    enum nexus_bt_hogp_state state;
    bool awaiting_response;
    uint8_t request_opcode;
    uint16_t client_mtu;
    uint16_t server_mtu;
    uint16_t next_handle;
    uint16_t report_map_offset;
    uint16_t current_descriptor_start;
    uint16_t current_descriptor_end;
    uint8_t report_index;
    uint8_t enabled_input_reports;
    struct nexus_bt_gatt_hid_service service;
    uint16_t characteristic_cursor;
    uint16_t previous_declaration_handle;
    uint8_t report_map[NEXUS_BT_HOGP_MAX_REPORT_MAP];
    uint16_t report_map_length;
};

/* Start a bounded HOGP client session. ATT requests are emitted one at a time
 * by next_request and matched with handle_response. */
bool nexus_bt_hogp_client_start(
    struct nexus_bt_hogp_client *client, uint16_t preferred_mtu);
bool nexus_bt_hogp_client_next_request(
    struct nexus_bt_hogp_client *client,
    uint8_t *pdu, size_t capacity, size_t *pdu_length);
bool nexus_bt_hogp_client_handle_response(
    struct nexus_bt_hogp_client *client,
    const uint8_t *pdu, size_t pdu_length);

bool nexus_bt_hogp_client_is_ready(
    const struct nexus_bt_hogp_client *client);
bool nexus_bt_hogp_client_get_report_map(
    const struct nexus_bt_hogp_client *client,
    const uint8_t **report_map, size_t *report_map_length);
bool nexus_bt_hogp_client_find_input_report(
    const struct nexus_bt_hogp_client *client,
    uint16_t value_handle, uint8_t *report_id);

#endif
