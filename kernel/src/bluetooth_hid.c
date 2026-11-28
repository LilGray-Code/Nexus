#include "bluetooth_hid.h"

#include "bluetooth_gatt.h"
#include "bluetooth_hci.h"
#include "input.h"

static uint16_t get_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}


static void put_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)(value >> 8);
}

bool nexus_bt_l2cap_parse_signaling_command(
    const struct nexus_bt_l2cap_view *l2cap,
    struct nexus_bt_l2cap_signaling_command *command) {
    if (l2cap == NULL || command == NULL || l2cap->payload == NULL ||
        l2cap->channel_id != NEXUS_BT_L2CAP_CID_SIGNALING ||
        l2cap->payload_length < 4U) return false;
    const uint8_t code = l2cap->payload[0];
    const uint8_t identifier = l2cap->payload[1];
    const uint16_t data_length = get_le16(l2cap->payload + 2U);
    if (code == 0U || identifier == 0U ||
        data_length > l2cap->payload_length - 4U) return false;
    *command = (struct nexus_bt_l2cap_signaling_command){
        .code = code,
        .identifier = identifier,
        .data = l2cap->payload + 4U,
        .data_length = data_length,
        .command_length = (uint16_t)(data_length + 4U)
    };
    return true;
}

bool nexus_bt_l2cap_hid_channel_begin(
    struct nexus_bt_l2cap_hid_channel *channel, uint16_t psm,
    uint16_t local_cid, uint8_t identifier,
    uint8_t *l2cap_packet, size_t capacity, size_t *packet_length) {
    if (channel == NULL || l2cap_packet == NULL || packet_length == NULL ||
        (psm != NEXUS_BT_HCI_PSM_HID_CONTROL &&
         psm != NEXUS_BT_HCI_PSM_HID_INTERRUPT) ||
        local_cid < 0x0040U || identifier == 0U || capacity < 12U ||
        channel->pending || channel->connected) return false;

    put_le16(l2cap_packet, 8U);
    put_le16(l2cap_packet + 2U, NEXUS_BT_L2CAP_CID_SIGNALING);
    l2cap_packet[4] = NEXUS_BT_L2CAP_SIGNAL_CONNECTION_REQUEST;
    l2cap_packet[5] = identifier;
    put_le16(l2cap_packet + 6U, 4U);
    put_le16(l2cap_packet + 8U, psm);
    put_le16(l2cap_packet + 10U, local_cid);
    *packet_length = 12U;
    *channel = (struct nexus_bt_l2cap_hid_channel){
        .psm = psm,
        .local_cid = local_cid,
        .identifier = identifier,
        .pending = true
    };
    return true;
}

bool nexus_bt_l2cap_hid_channel_handle_connection_response(
    struct nexus_bt_l2cap_hid_channel *channel,
    const struct nexus_bt_l2cap_signaling_command *command) {
    if (channel == NULL || command == NULL || !channel->pending ||
        command->code != NEXUS_BT_L2CAP_SIGNAL_CONNECTION_RESPONSE ||
        command->identifier != channel->identifier ||
        command->data == NULL || command->data_length != 8U)
        return false;
    const uint16_t remote_cid = get_le16(command->data);
    const uint16_t local_cid = get_le16(command->data + 2U);
    const uint16_t result = get_le16(command->data + 4U);
    const uint16_t status = get_le16(command->data + 6U);
    if (local_cid != channel->local_cid || result > 4U || status > 2U ||
        (result == 0U && (remote_cid < 0x0040U || status != 0U)))
        return false;

    channel->result = (uint8_t)result;
    channel->status = (uint8_t)status;
    if (result == 0U) {
        channel->remote_cid = remote_cid;
        channel->pending = false;
        channel->connected = true;
    } else if (result != 1U) {
        channel->pending = false;
        channel->connected = false;
        channel->remote_cid = 0U;
    }
    return true;
}

bool nexus_bt_l2cap_hid_channel_build_disconnect(
    struct nexus_bt_l2cap_hid_channel *channel, uint8_t identifier,
    uint8_t *l2cap_packet, size_t capacity, size_t *packet_length) {
    if (channel == NULL || !channel->connected ||
        channel->local_cid < 0x0040U || channel->remote_cid < 0x0040U ||
        channel->disconnect_pending || identifier == 0U ||
        l2cap_packet == NULL || packet_length == NULL || capacity < 12U)
        return false;
    put_le16(l2cap_packet, 8U);
    put_le16(l2cap_packet + 2U, NEXUS_BT_L2CAP_CID_SIGNALING);
    l2cap_packet[4] = NEXUS_BT_L2CAP_SIGNAL_DISCONNECTION_REQUEST;
    l2cap_packet[5] = identifier;
    put_le16(l2cap_packet + 6U, 4U);
    put_le16(l2cap_packet + 8U, channel->remote_cid);
    put_le16(l2cap_packet + 10U, channel->local_cid);
    *packet_length = 12U;
    channel->disconnect_identifier = identifier;
    channel->disconnect_pending = true;
    return true;
}

bool nexus_bt_l2cap_hid_channel_handle_disconnect_response(
    struct nexus_bt_l2cap_hid_channel *channel,
    const struct nexus_bt_l2cap_signaling_command *command) {
    if (channel == NULL || command == NULL || !channel->connected ||
        !channel->disconnect_pending ||
        command->code != NEXUS_BT_L2CAP_SIGNAL_DISCONNECTION_RESPONSE ||
        command->identifier != channel->disconnect_identifier ||
        command->data == NULL || command->data_length != 4U) return false;
    if (get_le16(command->data) != channel->remote_cid ||
        get_le16(command->data + 2U) != channel->local_cid) return false;
    channel->connected = false;
    channel->pending = false;
    channel->disconnect_pending = false;
    channel->disconnect_identifier = 0U;
    channel->remote_cid = 0U;
    return true;
}

bool nexus_bt_l2cap_parse(const uint8_t *packet, size_t packet_length,
                          struct nexus_bt_l2cap_view *l2cap) {
    if (packet == NULL || l2cap == NULL || packet_length < 4U)
        return false;
    const uint16_t payload_length = get_le16(packet);
    if ((size_t)payload_length != packet_length - 4U) return false;
    const uint16_t channel_id = get_le16(packet + 2U);
    if (channel_id == 0U) return false;
    *l2cap = (struct nexus_bt_l2cap_view){
        .channel_id = channel_id,
        .payload = packet + 4U,
        .payload_length = payload_length
    };
    return true;
}

bool nexus_bt_hidp_parse_input(const uint8_t *packet, size_t packet_length,
                               struct nexus_bt_hid_report_view *report) {
    if (packet == NULL || report == NULL || packet_length < 2U)
        return false;
    const uint8_t transaction = (uint8_t)(packet[0] >> 4);
    const uint8_t parameter = (uint8_t)(packet[0] & 0x0FU);
    if (transaction != NEXUS_BT_HIDP_TRANSACTION_DATA ||
        parameter != NEXUS_BT_HIDP_REPORT_INPUT) return false;
    *report = (struct nexus_bt_hid_report_view){
        .attribute_handle = 0U,
        .report = packet + 1U,
        .report_length = (uint16_t)(packet_length - 1U)
    };
    return true;
}

bool nexus_bt_hogp_parse_notification(
    const struct nexus_bt_l2cap_view *l2cap,
    struct nexus_bt_hid_report_view *report) {
    if (l2cap == NULL || report == NULL || l2cap->payload == NULL ||
        l2cap->channel_id != NEXUS_BT_L2CAP_CID_ATT ||
        l2cap->payload_length < 4U) return false;
    const uint8_t opcode = l2cap->payload[0];
    if (opcode != NEXUS_BT_ATT_HANDLE_VALUE_NOTIFICATION &&
        opcode != NEXUS_BT_ATT_HANDLE_VALUE_INDICATION) return false;
    const uint16_t handle = get_le16(l2cap->payload + 1U);
    if (handle == 0U) return false;
    const uint16_t value_length = (uint16_t)(l2cap->payload_length - 3U);
    if (value_length == 0U) return false;
    *report = (struct nexus_bt_hid_report_view){
        .attribute_handle = handle,
        .report = l2cap->payload + 3U,
        .report_length = value_length
    };
    return true;
}

bool nexus_bt_hogp_parse_read_response(
    const uint8_t *att_pdu, size_t pdu_length, uint16_t report_handle,
    struct nexus_bt_hid_report_view *report) {
    if (att_pdu == NULL || report == NULL || pdu_length < 2U ||
        report_handle == 0U ||
        att_pdu[0] != NEXUS_BT_ATT_READ_RESPONSE) return false;
    *report = (struct nexus_bt_hid_report_view){
        .attribute_handle = report_handle,
        .report = att_pdu + 1U,
        .report_length = (uint16_t)(pdu_length - 1U)
    };
    return true;
}

static bool bluetooth_hid_report_index(
    const struct nexus_bt_hid_input_device *device,
    const uint8_t *report, size_t report_length, uint8_t *index) {
    if (device == NULL || report == NULL || index == NULL ||
        device->layout.report_count == 0U ||
        device->layout.report_count > NEXUS_USB_HID_GENERIC_MAX_REPORTS)
        return false;

    const struct nexus_usb_hid_generic_layout *first =
        &device->layout.reports[0];
    if (!first->has_report_id) {
        if (device->layout.report_count != 1U) return false;
        *index = 0U;
        return true;
    }
    if (report_length == 0U) return false;
    for (uint8_t i = 0U; i < device->layout.report_count; ++i) {
        if (device->layout.reports[i].has_report_id &&
            device->layout.reports[i].report_id == report[0]) {
            *index = i;
            return true;
        }
    }
    return false;
}

static bool bluetooth_hid_dispatch_framed_report(
    struct nexus_bt_hid_input_device *device,
    const uint8_t *report, size_t report_length) {
    if (device == NULL || !device->configured || report == NULL ||
        report_length == 0U || report_length > NEXUS_BT_HID_MAX_REPORT_BYTES)
        return false;

    const enum nexus_usb_hid_generic_kind kind =
        device->layout.reports[0].kind;
    if (kind == NEXUS_USB_HID_GENERIC_MOUSE) {
        struct nexus_usb_mouse_event mouse = {0};
        if (!usb_hid_generic_decode_mouse_multi(&device->layout,
                report, report_length, &mouse)) return false;
        input_usb_mouse_event(&mouse);
        return true;
    }

    if (kind == NEXUS_USB_HID_GENERIC_KEYBOARD) {
        uint8_t boot_report[8] = {0};
        uint8_t report_index = 0U;
        if (!bluetooth_hid_report_index(device, report, report_length,
                &report_index) ||
            !usb_hid_generic_decode_keyboard_multi(&device->layout,
                report, report_length, boot_report)) return false;
        input_usb_keyboard_report_state(
            &device->keyboard_states[report_index], boot_report);
        return true;
    }
    return false;
}

bool nexus_bt_hid_input_configure(
    struct nexus_bt_hid_input_device *device,
    const uint8_t *report_map, size_t report_map_length) {
    if (device == NULL || report_map == NULL || report_map_length == 0U)
        return false;
    if (device->configured) nexus_bt_hid_input_disconnect(device);

    struct nexus_usb_hid_generic_multi_layout layout = {0};
    if (!usb_hid_generic_parse_descriptor_multi(
            report_map, report_map_length, &layout)) {
        *device = (struct nexus_bt_hid_input_device){0};
        return false;
    }
    *device = (struct nexus_bt_hid_input_device){
        .configured = true,
        .layout = layout
    };
    return true;
}

bool nexus_bt_hid_input_dispatch_hogp_report(
    struct nexus_bt_hid_input_device *device, uint8_t report_id,
    uint8_t report_type, const uint8_t *report, size_t report_length) {
    if (device == NULL || !device->configured || report == NULL ||
        report_length == 0U ||
        report_type != NEXUS_BT_HOGP_REPORT_TYPE_INPUT)
        return false;

    const bool has_report_ids =
        device->layout.reports[0].has_report_id;
    if (!has_report_ids) {
        if (report_id != 0U ||
            report_length > NEXUS_BT_HID_MAX_REPORT_BYTES) return false;
        return bluetooth_hid_dispatch_framed_report(
            device, report, report_length);
    }

    if (report_id == 0U ||
        report_length + 1U > NEXUS_BT_HID_MAX_REPORT_BYTES) return false;
    uint8_t framed[NEXUS_BT_HID_MAX_REPORT_BYTES];
    framed[0] = report_id;
    for (size_t i = 0U; i < report_length; ++i)
        framed[i + 1U] = report[i];
    return bluetooth_hid_dispatch_framed_report(
        device, framed, report_length + 1U);
}

bool nexus_bt_hid_input_dispatch_hogp_notification(
    struct nexus_bt_hid_input_device *device,
    const struct nexus_bt_gatt_hid_service *service,
    const struct nexus_bt_l2cap_view *l2cap) {
    struct nexus_bt_hid_report_view report = {0};
    if (service == NULL ||
        service->report_count > NEXUS_BT_GATT_MAX_REPORTS ||
        !nexus_bt_hogp_parse_notification(l2cap, &report)) return false;
    for (uint8_t i = 0U; i < service->report_count; ++i) {
        const struct nexus_bt_gatt_hid_report_characteristic *characteristic =
            &service->reports[i];
        if (characteristic->value_handle != report.attribute_handle)
            continue;
        return nexus_bt_hid_input_dispatch_hogp_report(
            device, characteristic->report_id, characteristic->report_type,
            report.report, report.report_length);
    }
    return false;
}

bool nexus_bt_hid_input_dispatch_hidp_packet(
    struct nexus_bt_hid_input_device *device,
    const uint8_t *packet, size_t packet_length) {
    struct nexus_bt_hid_report_view report = {0};
    if (!nexus_bt_hidp_parse_input(packet, packet_length, &report))
        return false;
    return bluetooth_hid_dispatch_framed_report(
        device, report.report, report.report_length);
}

void nexus_bt_hid_input_disconnect(
    struct nexus_bt_hid_input_device *device) {
    if (device == NULL || !device->configured) return;
    if (device->layout.reports[0].kind == NEXUS_USB_HID_GENERIC_MOUSE) {
        const struct nexus_usb_mouse_event released = {0};
        input_usb_mouse_event(&released);
    } else if (device->layout.reports[0].kind ==
               NEXUS_USB_HID_GENERIC_KEYBOARD) {
        const uint8_t released[8] = {0};
        for (uint8_t i = 0U; i < device->layout.report_count; ++i)
            input_usb_keyboard_report_state(
                &device->keyboard_states[i], released);
    }
    *device = (struct nexus_bt_hid_input_device){0};
}
