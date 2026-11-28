#include "bluetooth_hid.h"
#include "bluetooth_hci.h"
#include "bluetooth_gatt.h"
#include "input.h"

#include <assert.h>
#include <stdio.h>


static const uint8_t bluetooth_generic_mouse_map[] = {
    0x05,0x01, 0x09,0x02, 0xA1,0x01,
    0x09,0x01, 0xA1,0x00,
    0x05,0x09, 0x19,0x01, 0x29,0x03,
    0x15,0x00, 0x25,0x01, 0x95,0x03, 0x75,0x01, 0x81,0x02,
    0x95,0x01, 0x75,0x05, 0x81,0x01,
    0x05,0x01, 0x09,0x30, 0x09,0x31,
    0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x02, 0x81,0x06,
    0x09,0x38, 0x75,0x08, 0x95,0x01, 0x81,0x06,
    0xC0, 0xC0
};

static struct nexus_usb_mouse_event bluetooth_mouse_event;
static uint32_t bluetooth_mouse_submit_count;
static uint8_t bluetooth_keyboard_report[8];
static uint32_t bluetooth_keyboard_submit_count;

void input_usb_mouse_event(const struct nexus_usb_mouse_event *event) {
    assert(event != NULL);
    bluetooth_mouse_event = *event;
    ++bluetooth_mouse_submit_count;
}

void input_usb_keyboard_report_state(struct nexus_usb_keyboard_state *state,
                                     const uint8_t report[8]) {
    (void)state;
    assert(report != NULL);
    for (size_t i = 0U; i < sizeof(bluetooth_keyboard_report); ++i)
        bluetooth_keyboard_report[i] = report[i];
    ++bluetooth_keyboard_submit_count;
}

static void test_bluetooth_hogp_mouse_dispatch_and_release(void) {
    struct nexus_bt_hid_input_device device = {0};
    assert(nexus_bt_hid_input_configure(&device,
        bluetooth_generic_mouse_map, sizeof(bluetooth_generic_mouse_map)));
    assert(!nexus_bt_hid_input_set_security_ready(&device, false, true));
    const uint8_t early_report[4] = {0x01U,0U,0U,0U};
    assert(!nexus_bt_hid_input_dispatch_hogp_report(
        &device, 0U, NEXUS_BT_HOGP_REPORT_TYPE_INPUT,
        early_report, sizeof(early_report)));
    assert(nexus_bt_hid_input_set_security_ready(&device, true, true));
    const uint8_t report[4] = {0x05U,0xFEU,0x03U,0xFFU};
    const uint32_t before = bluetooth_mouse_submit_count;
    assert(nexus_bt_hid_input_dispatch_hogp_report(
        &device, 0U, NEXUS_BT_HOGP_REPORT_TYPE_INPUT,
        report, sizeof(report)));
    assert(bluetooth_mouse_submit_count == before + 1U);
    assert(bluetooth_mouse_event.buttons == 0x05U);
    assert(bluetooth_mouse_event.dx == -2);
    assert(bluetooth_mouse_event.dy == -3);
    assert(bluetooth_mouse_event.wheel == -1);

    assert(!nexus_bt_hid_input_dispatch_hogp_report(
        &device, 0U, 2U, report, sizeof(report)));
    nexus_bt_hid_input_disconnect(&device);
    assert(bluetooth_mouse_event.buttons == 0U);
    assert(!device.configured);
}

static void test_hogp_notification_matches_discovered_report_handle(void) {
    struct nexus_bt_hid_input_device device = {0};
    assert(nexus_bt_hid_input_configure(&device,
        bluetooth_generic_mouse_map, sizeof(bluetooth_generic_mouse_map)));
    struct nexus_bt_gatt_hid_service service = {0};
    service.report_count = 1U;
    service.reports[0].value_handle = 0x0020U;
    service.reports[0].report_id = 0U;
    service.reports[0].report_type = NEXUS_BT_HOGP_REPORT_TYPE_INPUT;

    const uint8_t packet[] = {
        7U,0U, 4U,0U, 0x1BU,0x20U,0U,
        0x05U,0xFEU,0x03U,0xFFU
    };
    struct nexus_bt_l2cap_view l2cap = {0};
    assert(nexus_bt_l2cap_parse(packet, sizeof(packet), &l2cap));
    const uint32_t before = bluetooth_mouse_submit_count;
    assert(nexus_bt_hid_input_dispatch_hogp_notification(
        &device, &service, &l2cap));
    assert(bluetooth_mouse_submit_count == before + 1U);
    assert(bluetooth_mouse_event.buttons == 0x05U);
    assert(bluetooth_mouse_event.dx == -2);
    assert(bluetooth_mouse_event.dy == -3);
    assert(bluetooth_mouse_event.wheel == -1);
    nexus_bt_hid_input_disconnect(&device);

    service.reports[0].value_handle = 0x0040U;
    assert(nexus_bt_hid_input_configure(&device,
        bluetooth_generic_mouse_map, sizeof(bluetooth_generic_mouse_map)));
    assert(!nexus_bt_hid_input_dispatch_hogp_notification(
        &device, &service, &l2cap));
}

static void test_bluetooth_hidp_mouse_dispatch(void) {
    struct nexus_bt_hid_input_device device = {0};
    assert(nexus_bt_hid_input_configure(&device,
        bluetooth_generic_mouse_map, sizeof(bluetooth_generic_mouse_map)));
    const uint8_t packet[] = {0xA1U,0x02U,0xFEU,0x03U,0xFFU};
    const uint32_t before = bluetooth_mouse_submit_count;
    assert(nexus_bt_hid_input_dispatch_hidp_packet(
        &device, packet, sizeof(packet)));
    assert(bluetooth_mouse_submit_count == before + 1U);
    assert(bluetooth_mouse_event.buttons == 0x02U);
    assert(bluetooth_mouse_event.dx == -2);
    assert(bluetooth_mouse_event.dy == -3);
    assert(bluetooth_mouse_event.wheel == -1);
    nexus_bt_hid_input_disconnect(&device);
}

static const uint8_t bluetooth_generic_keyboard_map[] = {
    0x05,0x01, 0x09,0x06, 0xA1,0x01,
    0x05,0x07, 0x19,0xE0, 0x29,0xE7,
    0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x75,0x08, 0x95,0x01, 0x81,0x01,
    0x19,0x00, 0x29,0x65, 0x15,0x00, 0x25,0x65,
    0x75,0x08, 0x95,0x06, 0x81,0x00,
    0xC0
};

static void test_bluetooth_keyboard_disconnect_releases_keys(void) {
    struct nexus_bt_hid_input_device device = {0};
    assert(nexus_bt_hid_input_configure(&device,
        bluetooth_generic_keyboard_map,
        sizeof(bluetooth_generic_keyboard_map)));
    assert(nexus_bt_hid_input_set_security_ready(&device, true, true));
    const uint8_t report[8] = {0U,0U,0x04U,0U,0U,0U,0U,0U};
    const uint32_t before = bluetooth_keyboard_submit_count;
    assert(nexus_bt_hid_input_dispatch_hogp_report(
        &device, 0U, NEXUS_BT_HOGP_REPORT_TYPE_INPUT,
        report, sizeof(report)));
    assert(bluetooth_keyboard_submit_count == before + 1U);
    assert(bluetooth_keyboard_report[2] == 0x04U);
    nexus_bt_hid_input_disconnect(&device);
    assert(bluetooth_keyboard_submit_count == before + 2U);
    assert(bluetooth_keyboard_report[2] == 0U);
}


static void test_classic_hid_l2cap_channel_lifecycle(void) {
    struct nexus_bt_l2cap_hid_channel channel = {0};
    uint8_t packet[16] = {0};
    size_t packet_length = 0U;
    assert(nexus_bt_l2cap_hid_channel_begin(
        &channel, NEXUS_BT_HCI_PSM_HID_CONTROL, 0x0040U, 0x11U,
        packet, sizeof(packet), &packet_length));
    assert(packet_length == 12U);
    assert(packet[0] == 8U && packet[2] == 1U);
    assert(packet[4] == NEXUS_BT_L2CAP_SIGNAL_CONNECTION_REQUEST);
    assert(packet[5] == 0x11U);
    assert(packet[8] == 0x11U && packet[9] == 0U);
    assert(packet[10] == 0x40U && packet[11] == 0U);

    struct nexus_bt_l2cap_view l2cap = {0};
    struct nexus_bt_l2cap_signaling_command command = {0};
    assert(nexus_bt_l2cap_parse(packet, packet_length, &l2cap));
    assert(nexus_bt_l2cap_parse_signaling_command(&l2cap, &command));
    assert(command.code == NEXUS_BT_L2CAP_SIGNAL_CONNECTION_REQUEST);
    assert(command.identifier == 0x11U && command.data_length == 4U);

    const uint8_t accepted_data[] = {
        0x41U,0x00U, 0x40U,0x00U, 0U,0U, 0U,0U
    };
    const struct nexus_bt_l2cap_signaling_command accepted = {
        .code = NEXUS_BT_L2CAP_SIGNAL_CONNECTION_RESPONSE,
        .identifier = 0x11U,
        .data = accepted_data,
        .data_length = sizeof(accepted_data)
    };
    assert(nexus_bt_l2cap_hid_channel_handle_connection_response(
        &channel, &accepted));
    assert(channel.connected && !channel.pending);
    assert(channel.remote_cid == 0x0041U);

    assert(nexus_bt_l2cap_hid_channel_build_disconnect(
        &channel, 0x12U, packet, sizeof(packet), &packet_length));
    assert(packet_length == 12U);
    assert(packet[4] == NEXUS_BT_L2CAP_SIGNAL_DISCONNECTION_REQUEST);
    assert(packet[5] == 0x12U);
    assert(packet[8] == 0x41U && packet[10] == 0x40U);
    const uint8_t disconnect_data[] = {0x41U,0x00U,0x40U,0x00U};
    const struct nexus_bt_l2cap_signaling_command disconnected = {
        .code = NEXUS_BT_L2CAP_SIGNAL_DISCONNECTION_RESPONSE,
        .identifier = 0x12U,
        .data = disconnect_data,
        .data_length = sizeof(disconnect_data)
    };
    assert(nexus_bt_l2cap_hid_channel_handle_disconnect_response(
        &channel, &disconnected));
    assert(!channel.connected && !channel.disconnect_pending);
    assert(channel.remote_cid == 0U);
}

static void test_classic_hid_l2cap_channel_rejects_mismatch(void) {
    struct nexus_bt_l2cap_hid_channel channel = {0};
    uint8_t packet[12] = {0};
    size_t packet_length = 0U;
    assert(nexus_bt_l2cap_hid_channel_begin(
        &channel, NEXUS_BT_HCI_PSM_HID_INTERRUPT, 0x0042U, 0x21U,
        packet, sizeof(packet), &packet_length));
    const uint8_t bad_data[] = {
        0x44U,0x00U, 0x43U,0x00U, 0U,0U, 0U,0U
    };
    const struct nexus_bt_l2cap_signaling_command bad_response = {
        .code = NEXUS_BT_L2CAP_SIGNAL_CONNECTION_RESPONSE,
        .identifier = 0x21U,
        .data = bad_data,
        .data_length = sizeof(bad_data)
    };
    assert(!nexus_bt_l2cap_hid_channel_handle_connection_response(
        &channel, &bad_response));
    assert(channel.pending && !channel.connected);
    assert(!nexus_bt_l2cap_hid_channel_begin(
        &channel, 0x0001U, 0x0043U, 0x22U,
        packet, sizeof(packet), &packet_length));
}

int main(void) {
    const uint8_t l2cap_packet[] = {4U,0U,4U,0U,0x1BU,0x20U,0U,0x04U};
    struct nexus_bt_l2cap_view l2cap = {0};
    assert(nexus_bt_l2cap_parse(l2cap_packet, sizeof(l2cap_packet), &l2cap));
    assert(l2cap.channel_id == NEXUS_BT_L2CAP_CID_ATT);
    assert(l2cap.payload_length == 4U);

    struct nexus_bt_hid_report_view report = {0};
    assert(nexus_bt_hogp_parse_notification(&l2cap, &report));
    assert(report.attribute_handle == 0x0020U);
    assert(report.report_length == 1U && report.report[0] == 0x04U);

    const uint8_t hidp[] = {0xA1U,0x02U,0x00U,0x04U};
    assert(nexus_bt_hidp_parse_input(hidp, sizeof(hidp), &report));
    assert(report.attribute_handle == 0U && report.report_length == 3U);
    assert(report.report[0] == 0x02U);
    const uint8_t hidp_output[] = {0xA2U,0x00U};
    assert(!nexus_bt_hidp_parse_input(hidp_output,
        sizeof(hidp_output), &report));

    const uint8_t att_read[] = {0x0BU,0x01U,0x02U,0x03U};
    assert(nexus_bt_hogp_parse_read_response(att_read,
        sizeof(att_read), 0x0042U, &report));
    assert(report.attribute_handle == 0x0042U);
    assert(report.report_length == 3U && report.report[2] == 0x03U);

    const uint8_t malformed_l2cap[] = {5U,0U,4U,0U,0x1BU,0x20U,0U,0x04U};
    assert(!nexus_bt_l2cap_parse(malformed_l2cap,
        sizeof(malformed_l2cap), &l2cap));
    test_bluetooth_hogp_mouse_dispatch_and_release();
    test_hogp_notification_matches_discovered_report_handle();
    test_bluetooth_hidp_mouse_dispatch();
    test_bluetooth_keyboard_disconnect_releases_keys();
    test_classic_hid_l2cap_channel_lifecycle();
    test_classic_hid_l2cap_channel_rejects_mismatch();
    puts("NEXUS BLUETOOTH HIDP/HOGP INPUT DELIVERY TESTS PASSED");
    return 0;
}
