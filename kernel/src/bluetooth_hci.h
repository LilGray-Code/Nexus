#ifndef NEXUS_BLUETOOTH_HCI_H
#define NEXUS_BLUETOOTH_HCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* HCI packet type values used by USB/UART HCI transports. */
#define NEXUS_BT_HCI_PACKET_COMMAND 0x01U
#define NEXUS_BT_HCI_PACKET_ACL     0x02U
#define NEXUS_BT_HCI_PACKET_EVENT   0x04U
#define NEXUS_BT_HCI_EVENT_CONNECTION_COMPLETE 0x03U
#define NEXUS_BT_HCI_EVENT_DISCONNECTION_COMPLETE 0x05U
#define NEXUS_BT_HCI_EVENT_LE_META 0x3EU
#define NEXUS_BT_HCI_LE_CONNECTION_COMPLETE 0x01U
#define NEXUS_BT_HCI_PSM_HID_CONTROL 0x0011U
#define NEXUS_BT_HCI_PSM_HID_INTERRUPT 0x0013U
#define NEXUS_BT_HCI_CID_ATT 0x0004U
#define NEXUS_BT_HID_SERVICE_UUID 0x1812U
#define NEXUS_BT_HID_REPORT_MAP_UUID 0x2A4BU
#define NEXUS_BT_HID_REPORT_UUID 0x2A4DU

struct nexus_bt_hci_event_view {
    uint8_t event_code;
    const uint8_t *parameters;
    uint8_t parameter_length;
};

struct nexus_bt_hci_le_advertising_report {
    uint8_t event_type;
    uint8_t address_type;
    uint8_t peer_address[6];
    const uint8_t *data;
    uint8_t data_length;
    int8_t rssi;
};

struct nexus_bt_hci_acl_view {
    uint16_t connection_handle;
    uint8_t packet_boundary;
    uint8_t broadcast;
    const uint8_t *payload;
    uint16_t payload_length;
};


#define NEXUS_BT_HCI_MAX_L2CAP_PACKET 1024U

enum nexus_bt_hci_acl_reassembly_status {
    NEXUS_BT_HCI_ACL_REASSEMBLY_INVALID = 0,
    NEXUS_BT_HCI_ACL_REASSEMBLY_PENDING,
    NEXUS_BT_HCI_ACL_REASSEMBLY_COMPLETE
};

struct nexus_bt_hci_acl_reassembler {
    bool active;
    uint16_t connection_handle;
    uint16_t expected_length;
    uint16_t accumulated_length;
    uint8_t packet[NEXUS_BT_HCI_MAX_L2CAP_PACKET];
};

struct nexus_bt_hci_l2cap_packet {
    uint16_t connection_handle;
    const uint8_t *packet;
    uint16_t packet_length;
};

/* Completed packet data is owned by the reassembler and must be consumed
 * before its buffer is reused by the next call. */
void nexus_bt_hci_acl_reassembler_reset(
    struct nexus_bt_hci_acl_reassembler *reassembler);
enum nexus_bt_hci_acl_reassembly_status nexus_bt_hci_acl_reassemble(
    struct nexus_bt_hci_acl_reassembler *reassembler,
    const struct nexus_bt_hci_acl_view *fragment,
    struct nexus_bt_hci_l2cap_packet *completed);

struct nexus_bt_hci_connection {
    bool connected;
    bool low_energy;
    uint16_t handle;
    uint8_t peer_address[6];
};

/* Strict HCI framing validation: the transport packet must contain exactly
 * the declared event/ACL payload, with no truncated or overlong packet. */
bool nexus_bt_hci_parse_event(const uint8_t *packet, size_t packet_length,
                              struct nexus_bt_hci_event_view *event);
bool nexus_bt_hci_parse_acl(const uint8_t *packet, size_t packet_length,
                            struct nexus_bt_hci_acl_view *acl);
bool nexus_bt_hci_parse_le_advertising_report(
    const struct nexus_bt_hci_event_view *event, uint8_t report_index,
    struct nexus_bt_hci_le_advertising_report *report,
    uint8_t *total_reports);
bool nexus_bt_hci_connection_complete(
    const struct nexus_bt_hci_event_view *event,
    struct nexus_bt_hci_connection *connection);
bool nexus_bt_hci_disconnection_complete(
    const struct nexus_bt_hci_event_view *event,
    uint16_t *connection_handle, uint8_t *reason);


#define NEXUS_BT_HCI_EVENT_COMMAND_COMPLETE 0x0EU
#define NEXUS_BT_HCI_EVENT_COMMAND_STATUS 0x0FU
#define NEXUS_BT_HCI_OPCODE_RESET 0x0C03U
#define NEXUS_BT_HCI_OPCODE_READ_LOCAL_VERSION 0x1001U
#define NEXUS_BT_HCI_OPCODE_READ_SUPPORTED_COMMANDS 0x1002U
#define NEXUS_BT_HCI_OPCODE_READ_BD_ADDR 0x1009U
#define NEXUS_BT_HCI_OPCODE_LE_SET_SCAN_PARAMETERS 0x200BU
#define NEXUS_BT_HCI_OPCODE_LE_SET_SCAN_ENABLE 0x200CU
#define NEXUS_BT_HCI_OPCODE_LE_CREATE_CONNECTION 0x200DU
#define NEXUS_BT_HCI_LE_ADVERTISING_REPORT 0x02U
#define NEXUS_BT_HCI_MAX_LE_ADVERTISING_REPORTS 25U

struct nexus_bt_hci_command_result {
    uint16_t opcode;
    uint8_t status;
    uint8_t command_packets_allowed;
    const uint8_t *return_parameters;
    uint8_t return_parameter_length;
};

bool nexus_bt_hci_build_command(uint16_t opcode,
    const uint8_t *parameters, uint8_t parameter_length,
    uint8_t *packet, size_t packet_capacity, size_t *packet_length);
bool nexus_bt_hci_parse_command_complete(
    const struct nexus_bt_hci_event_view *event,
    struct nexus_bt_hci_command_result *result);
bool nexus_bt_hci_parse_command_status(
    const struct nexus_bt_hci_event_view *event,
    struct nexus_bt_hci_command_result *result);


enum nexus_bt_hci_host_state {
    NEXUS_BT_HCI_HOST_STOPPED = 0,
    NEXUS_BT_HCI_HOST_RESET_PENDING,
    NEXUS_BT_HCI_HOST_RESET_IN_FLIGHT,
    NEXUS_BT_HCI_HOST_VERSION_PENDING,
    NEXUS_BT_HCI_HOST_VERSION_IN_FLIGHT,
    NEXUS_BT_HCI_HOST_ADDRESS_PENDING,
    NEXUS_BT_HCI_HOST_ADDRESS_IN_FLIGHT,
    NEXUS_BT_HCI_HOST_READY,
    NEXUS_BT_HCI_HOST_LE_SCAN_PARAMETERS_PENDING,
    NEXUS_BT_HCI_HOST_LE_SCAN_PARAMETERS_IN_FLIGHT,
    NEXUS_BT_HCI_HOST_LE_SCAN_ENABLE_PENDING,
    NEXUS_BT_HCI_HOST_LE_SCAN_ENABLE_IN_FLIGHT,
    NEXUS_BT_HCI_HOST_SCANNING,
    NEXUS_BT_HCI_HOST_LE_SCAN_DISABLE_PENDING,
    NEXUS_BT_HCI_HOST_LE_SCAN_DISABLE_IN_FLIGHT,
    NEXUS_BT_HCI_HOST_FAILED
};

struct nexus_bt_hci_host {
    enum nexus_bt_hci_host_state state;
    uint8_t command_credits;
    uint16_t outstanding_opcode;
    uint8_t hci_version;
    uint8_t lmp_version;
    uint16_t manufacturer;
    uint16_t subversion;
    uint8_t controller_address[6];
    uint16_t le_scan_interval;
    uint16_t le_scan_window;
    bool le_scan_active;
    bool le_scan_filter_duplicates;
    bool le_scan_enable;
};

/* Minimal serialized controller bring-up. The caller owns the USB/UART
 * transport and must submit the returned HCI command packet and feed received
 * HCI events back into the host state machine. */
void nexus_bt_hci_host_init(struct nexus_bt_hci_host *host);
bool nexus_bt_hci_host_next_command(struct nexus_bt_hci_host *host,
    uint8_t *packet, size_t capacity, size_t *packet_length);
bool nexus_bt_hci_host_handle_event(struct nexus_bt_hci_host *host,
    const struct nexus_bt_hci_event_view *event);
bool nexus_bt_hci_host_start_le_scan(
    struct nexus_bt_hci_host *host, uint16_t interval, uint16_t window,
    bool active_scan, bool filter_duplicates);
bool nexus_bt_hci_host_stop_le_scan(struct nexus_bt_hci_host *host);

struct nexus_bt_hci_le_connection_parameters {
    uint16_t scan_interval;
    uint16_t scan_window;
    uint8_t peer_address_type;
    uint8_t peer_address[6];
    uint8_t own_address_type;
    uint16_t connection_interval_min;
    uint16_t connection_interval_max;
    uint16_t latency;
    uint16_t supervision_timeout;
    uint16_t minimum_connection_event_length;
    uint16_t maximum_connection_event_length;
};

bool nexus_bt_hci_build_le_create_connection(
    const struct nexus_bt_hci_le_connection_parameters *parameters,
    uint8_t *packet, size_t capacity, size_t *packet_length);

#endif
