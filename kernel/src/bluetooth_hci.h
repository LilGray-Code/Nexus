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
#define NEXUS_BT_HCI_EVENT_ENCRYPTION_CHANGE 0x08U
#define NEXUS_BT_HCI_EVENT_INQUIRY_COMPLETE 0x01U
#define NEXUS_BT_HCI_EVENT_INQUIRY_RESULT 0x02U
#define NEXUS_BT_HCI_EVENT_INQUIRY_RESULT_RSSI 0x22U
#define NEXUS_BT_HCI_EVENT_LE_META 0x3EU
#define NEXUS_BT_HCI_LE_ADVERTISING_REPORT 0x02U
#define NEXUS_BT_HCI_OPCODE_INQUIRY 0x0401U
#define NEXUS_BT_HCI_OPCODE_INQUIRY_CANCEL 0x0402U
#define NEXUS_BT_HCI_OPCODE_CREATE_CONNECTION 0x0405U
#define NEXUS_BT_HCI_EVENT_AUTHENTICATION_COMPLETE 0x06U
#define NEXUS_BT_HCI_EVENT_LINK_KEY_REQUEST 0x17U
#define NEXUS_BT_HCI_EVENT_LINK_KEY_NOTIFICATION 0x18U
#define NEXUS_BT_HCI_EVENT_IO_CAPABILITY_REQUEST 0x31U
#define NEXUS_BT_HCI_EVENT_IO_CAPABILITY_RESPONSE 0x32U
#define NEXUS_BT_HCI_EVENT_USER_CONFIRMATION_REQUEST 0x33U
#define NEXUS_BT_HCI_EVENT_USER_PASSKEY_REQUEST 0x34U
#define NEXUS_BT_HCI_EVENT_SIMPLE_PAIRING_COMPLETE 0x36U
#define NEXUS_BT_HCI_OPCODE_LINK_KEY_REQUEST_REPLY 0x040BU
#define NEXUS_BT_HCI_OPCODE_LINK_KEY_REQUEST_NEGATIVE_REPLY 0x040CU
#define NEXUS_BT_HCI_OPCODE_AUTHENTICATION_REQUESTED 0x0411U
#define NEXUS_BT_HCI_OPCODE_SET_CONNECTION_ENCRYPTION 0x0413U
#define NEXUS_BT_HCI_OPCODE_IO_CAPABILITY_REQUEST_REPLY 0x042BU
#define NEXUS_BT_HCI_OPCODE_USER_CONFIRMATION_REQUEST_REPLY 0x042CU
#define NEXUS_BT_HCI_OPCODE_USER_CONFIRMATION_REQUEST_NEGATIVE_REPLY 0x042DU
#define NEXUS_BT_HCI_OPCODE_USER_PASSKEY_REQUEST_NEGATIVE_REPLY 0x042FU
#define NEXUS_BT_HCI_OPCODE_IO_CAPABILITY_REQUEST_NEGATIVE_REPLY 0x0434U
#define NEXUS_BT_HCI_OPCODE_LE_SET_SCAN_PARAMETERS 0x200BU
#define NEXUS_BT_HCI_OPCODE_LE_SET_SCAN_ENABLE 0x200CU
#define NEXUS_BT_HCI_OPCODE_LE_CREATE_CONNECTION 0x200DU
#define NEXUS_BT_HCI_OPCODE_LE_START_ENCRYPTION 0x2019U
#define NEXUS_BT_HCI_OPCODE_SET_EVENT_MASK 0x0C01U
#define NEXUS_BT_HCI_OPCODE_LE_SET_EVENT_MASK 0x2001U
#define NEXUS_BT_HCI_OPCODE_LE_READ_LOCAL_P256_PUBLIC_KEY 0x2025U
#define NEXUS_BT_HCI_OPCODE_LE_GENERATE_DHKEY 0x2026U
#define NEXUS_BT_HCI_LE_READ_LOCAL_P256_PUBLIC_KEY_COMPLETE 0x08U
#define NEXUS_BT_HCI_LE_GENERATE_DHKEY_COMPLETE 0x09U
#define NEXUS_BT_HCI_LE_CONNECTION_COMPLETE 0x01U
#define NEXUS_BT_HCI_LE_ENHANCED_CONNECTION_COMPLETE 0x0AU
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

struct nexus_bt_hci_acl_view {
    uint16_t connection_handle;
    uint8_t packet_boundary;
    uint8_t broadcast;
    const uint8_t *payload;
    uint16_t payload_length;
};

struct nexus_bt_hci_connection {
    bool connected;
    bool low_energy;
    bool encrypted;
    uint16_t handle;
    uint8_t peer_address_type;
    uint8_t peer_address[6];
};

#define NEXUS_BT_HCI_MAX_CLASSIC_RESULTS 16U
struct nexus_bt_hci_classic_device {
    uint8_t address[6];
    uint8_t page_scan_repetition_mode;
    uint8_t class_of_device[3];
    uint16_t clock_offset;
    int8_t rssi;
    bool rssi_valid;
};
bool nexus_bt_hci_parse_inquiry_results(
    const struct nexus_bt_hci_event_view *event,
    struct nexus_bt_hci_classic_device *devices,
    size_t device_capacity, size_t *device_count);
bool nexus_bt_hci_parse_inquiry_complete(
    const struct nexus_bt_hci_event_view *event, uint8_t *status);
bool nexus_bt_hci_build_inquiry(
    uint8_t inquiry_length, uint8_t max_responses,
    uint8_t *packet, size_t packet_capacity, size_t *packet_length);
bool nexus_bt_hci_build_inquiry_cancel(
    uint8_t *packet, size_t packet_capacity, size_t *packet_length);
bool nexus_bt_hci_build_create_connection(
    const uint8_t address[6], uint16_t packet_type,
    uint8_t page_scan_repetition_mode, uint16_t clock_offset,
    bool allow_role_switch, uint8_t *packet, size_t packet_capacity,
    size_t *packet_length);

#define NEXUS_BT_HCI_MAX_ADV_DATA 31U
#define NEXUS_BT_HCI_MAX_ADV_REPORTS 25U

struct nexus_bt_hci_advertisement {
    uint8_t event_type;
    uint8_t address_type;
    uint8_t address[6];
    uint8_t data_length;
    uint8_t data[NEXUS_BT_HCI_MAX_ADV_DATA];
    int8_t rssi;
};

#define NEXUS_BT_ACL_MAX_L2CAP_PDU 1024U

enum nexus_bt_acl_reassembly_result {
    NEXUS_BT_ACL_REJECTED = 0,
    NEXUS_BT_ACL_PENDING = 1,
    NEXUS_BT_ACL_COMPLETE = 2
};

/* One bounded in-progress L2CAP PDU. The caller should keep one instance per
 * active HCI connection handle and feed ACL packets in controller order. */
struct nexus_bt_acl_reassembler {
    bool active;
    bool complete;
    uint16_t connection_handle;
    uint16_t expected_length;
    uint16_t received_length;
    uint8_t pdu[NEXUS_BT_ACL_MAX_L2CAP_PDU];
};

void nexus_bt_acl_reassembler_reset(
    struct nexus_bt_acl_reassembler *reassembler);
enum nexus_bt_acl_reassembly_result nexus_bt_acl_reassembler_feed(
    struct nexus_bt_acl_reassembler *reassembler,
    const struct nexus_bt_hci_acl_view *acl);
/* A too-small destination reports the required length and preserves the
 * completed PDU so the consumer can retry with a sufficiently large buffer. */
bool nexus_bt_acl_reassembler_read(
    struct nexus_bt_acl_reassembler *reassembler,
    uint8_t *pdu, size_t capacity, size_t *pdu_length);

/* Strict HCI framing validation: the transport packet must contain exactly
 * the declared event/ACL payload, with no truncated or overlong packet. */
bool nexus_bt_hci_parse_event(const uint8_t *packet, size_t packet_length,
                              struct nexus_bt_hci_event_view *event);
bool nexus_bt_hci_parse_acl(const uint8_t *packet, size_t packet_length,
                            struct nexus_bt_hci_acl_view *acl);
bool nexus_bt_hci_connection_complete(
    const struct nexus_bt_hci_event_view *event,
    struct nexus_bt_hci_connection *connection);
bool nexus_bt_hci_disconnection_complete(
    const struct nexus_bt_hci_event_view *event,
    uint16_t *connection_handle, uint8_t *reason);

/* Parse all legacy LE Advertising Report structures in one LE Meta Event.
 * A report's data is copied into caller-owned bounded storage. */
bool nexus_bt_hci_parse_le_advertising_reports(
    const struct nexus_bt_hci_event_view *event,
    struct nexus_bt_hci_advertisement *reports,
    size_t report_capacity, size_t *report_count);
/* Decode the complete/short local-name AD structure from a legacy report.
 * Output is printable ASCII and always NUL-terminated when capacity > 0. */
bool nexus_bt_hci_advertisement_copy_name(
    const struct nexus_bt_hci_advertisement *advertisement,
    char *name, size_t name_capacity);

bool nexus_bt_hci_build_le_scan_parameters(
    uint8_t scan_type, uint16_t scan_interval, uint16_t scan_window,
    uint8_t own_address_type, uint8_t filter_policy,
    uint8_t *packet, size_t packet_capacity, size_t *packet_length);
bool nexus_bt_hci_build_le_scan_enable(
    bool enable, bool filter_duplicates,
    uint8_t *packet, size_t packet_capacity, size_t *packet_length);
bool nexus_bt_hci_build_le_start_encryption(
    uint16_t connection_handle, const uint8_t random[8], uint16_t ediv,
    const uint8_t ltk[16], uint8_t *packet, size_t packet_capacity,
    size_t *packet_length);

bool nexus_bt_hci_build_set_event_mask(
    uint64_t event_mask, uint8_t *packet, size_t packet_capacity,
    size_t *packet_length);
bool nexus_bt_hci_build_le_set_event_mask(
    uint64_t le_event_mask, uint8_t *packet, size_t packet_capacity,
    size_t *packet_length);
bool nexus_bt_hci_build_le_read_local_p256_public_key(
    uint8_t *packet, size_t packet_capacity, size_t *packet_length);
bool nexus_bt_hci_build_le_generate_dhkey(
    const uint8_t remote_public_key[64],
    uint8_t *packet, size_t packet_capacity, size_t *packet_length);
bool nexus_bt_hci_parse_le_read_local_p256_public_key_complete(
    const struct nexus_bt_hci_event_view *event,
    uint8_t public_key[64]);
bool nexus_bt_hci_parse_le_generate_dhkey_complete(
    const struct nexus_bt_hci_event_view *event,
    uint8_t dhkey[32]);
bool nexus_bt_hci_parse_encryption_change(
    const struct nexus_bt_hci_event_view *event,
    uint16_t *connection_handle, uint8_t *status, bool *enabled);

/* BR/EDR SSP support is intentionally NoInputNoOutput, No MITM and
 * non-bonding. Link keys are session-ephemeral and must not be persisted. */
bool nexus_bt_hci_parse_classic_address_event(
    const struct nexus_bt_hci_event_view *event,
    uint8_t expected_event_code, uint8_t address[6]);
bool nexus_bt_hci_parse_io_capability_response(
    const struct nexus_bt_hci_event_view *event,
    uint8_t address[6], uint8_t *io_capability,
    uint8_t *oob_data_present, uint8_t *authentication_requirements);
bool nexus_bt_hci_parse_link_key_notification(
    const struct nexus_bt_hci_event_view *event,
    uint8_t address[6], uint8_t link_key[16], uint8_t *key_type);
bool nexus_bt_hci_parse_authentication_complete(
    const struct nexus_bt_hci_event_view *event,
    uint16_t *connection_handle, uint8_t *status);
bool nexus_bt_hci_parse_simple_pairing_complete(
    const struct nexus_bt_hci_event_view *event,
    uint8_t address[6], uint8_t *status);

bool nexus_bt_hci_build_authentication_requested(
    uint16_t connection_handle, uint8_t *packet,
    size_t capacity, size_t *packet_length);
bool nexus_bt_hci_build_set_connection_encryption(
    uint16_t connection_handle, bool enable, uint8_t *packet,
    size_t capacity, size_t *packet_length);
bool nexus_bt_hci_build_link_key_request_reply(
    const uint8_t address[6], const uint8_t link_key[16],
    uint8_t *packet, size_t capacity, size_t *packet_length);
bool nexus_bt_hci_build_link_key_request_negative_reply(
    const uint8_t address[6], uint8_t *packet,
    size_t capacity, size_t *packet_length);
bool nexus_bt_hci_build_io_capability_request_reply(
    const uint8_t address[6], uint8_t io_capability,
    uint8_t oob_data_present, uint8_t authentication_requirements,
    uint8_t *packet, size_t capacity, size_t *packet_length);
bool nexus_bt_hci_build_io_capability_request_negative_reply(
    const uint8_t address[6], uint8_t reason,
    uint8_t *packet, size_t capacity, size_t *packet_length);
bool nexus_bt_hci_build_user_confirmation_reply(
    const uint8_t address[6], bool accept,
    uint8_t *packet, size_t capacity, size_t *packet_length);
bool nexus_bt_hci_build_user_passkey_negative_reply(
    const uint8_t address[6], uint8_t *packet,
    size_t capacity, size_t *packet_length);

bool nexus_bt_hci_build_le_create_connection(
    uint16_t scan_interval, uint16_t scan_window,
    uint8_t initiator_filter_policy, uint8_t peer_address_type,
    const uint8_t peer_address[6], uint8_t own_address_type,
    uint16_t connection_interval_min, uint16_t connection_interval_max,
    uint16_t connection_latency, uint16_t supervision_timeout,
    uint16_t connection_event_length_min,
    uint16_t connection_event_length_max,
    uint8_t *packet, size_t packet_capacity, size_t *packet_length);


#define NEXUS_BT_HCI_EVENT_COMMAND_COMPLETE 0x0EU
#define NEXUS_BT_HCI_EVENT_COMMAND_STATUS 0x0FU
#define NEXUS_BT_HCI_OPCODE_RESET 0x0C03U
#define NEXUS_BT_HCI_OPCODE_READ_LOCAL_VERSION 0x1001U
#define NEXUS_BT_HCI_OPCODE_READ_SUPPORTED_COMMANDS 0x1002U
#define NEXUS_BT_HCI_OPCODE_READ_BD_ADDR 0x1009U

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
    NEXUS_BT_HCI_HOST_FAILED
};

struct nexus_bt_hci_host {
    enum nexus_bt_hci_host_state state;
    uint8_t command_credits;
    uint16_t outstanding_opcode;
    uint16_t last_completed_opcode;
    uint8_t last_command_status;
    uint8_t hci_version;
    uint8_t lmp_version;
    uint16_t manufacturer;
    uint16_t subversion;
    uint8_t controller_address[6];
};

/* Minimal serialized controller bring-up. The caller owns the USB/UART
 * transport and must submit the returned HCI command packet and feed received
 * HCI events back into the host state machine. */
void nexus_bt_hci_host_init(struct nexus_bt_hci_host *host);
bool nexus_bt_hci_host_next_command(struct nexus_bt_hci_host *host,
    uint8_t *packet, size_t capacity, size_t *packet_length);
/* Queue one additional command after initialization. Command-complete or
 * command-status events are matched against the outstanding opcode. */
bool nexus_bt_hci_host_send_command(struct nexus_bt_hci_host *host,
    uint16_t opcode, const uint8_t *parameters, uint8_t parameter_length,
    uint8_t *packet, size_t capacity, size_t *packet_length);
bool nexus_bt_hci_host_handle_event(struct nexus_bt_hci_host *host,
    const struct nexus_bt_hci_event_view *event);

#endif
