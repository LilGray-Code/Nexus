#include "bluetooth_classic_hid.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void pop_and_ack(
    struct nexus_bt_classic_hid_session *session,
    uint16_t expected_channel, const uint8_t *expected, size_t expected_length) {
    uint16_t channel = 0U;
    uint8_t payload[32] = {0U};
    size_t length = 0U;
    assert(nexus_bt_classic_hid_next_tx(
        session,&channel,payload,sizeof(payload),&length));
    assert(channel == expected_channel && length == expected_length);
    assert(memcmp(payload,expected,length) == 0);
    nexus_bt_classic_hid_tx_complete(session,true);
}

static void feed_signal(struct nexus_bt_classic_hid_session *session,
                        const uint8_t *signal, size_t signal_length) {
    uint8_t l2cap[64] = {0U};
    assert(signal_length <= sizeof(l2cap) - 4U);
    l2cap[0] = (uint8_t)signal_length;
    l2cap[1] = (uint8_t)(signal_length >> 8);
    l2cap[2] = 1U;
    memcpy(l2cap + 4U,signal,signal_length);
    assert(nexus_bt_classic_hid_handle_l2cap(
        session,l2cap,signal_length + 4U,NULL));
}

static void feed_connection_response(
    struct nexus_bt_classic_hid_session *session,
    uint8_t id, uint16_t remote_cid, uint16_t local_cid) {
    const uint8_t response[] = {
        0x03U,id,8U,0U,
        (uint8_t)remote_cid,(uint8_t)(remote_cid >> 8),
        (uint8_t)local_cid,(uint8_t)(local_cid >> 8),
        0U,0U,0U,0U
    };
    feed_signal(session,response,sizeof(response));
}

static void feed_config_response(
    struct nexus_bt_classic_hid_session *session,
    uint8_t id, uint16_t remote_cid) {
    const uint8_t response[] = {
        0x05U,id,6U,0U,
        (uint8_t)remote_cid,(uint8_t)(remote_cid >> 8),
        0U,0U,0U,0U
    };
    feed_signal(session,response,sizeof(response));
}

static void feed_peer_config_request(
    struct nexus_bt_classic_hid_session *session,
    uint8_t id, uint16_t local_cid) {
    const uint8_t request[] = {
        0x04U,id,4U,0U,
        (uint8_t)local_cid,(uint8_t)(local_cid >> 8),0U,0U
    };
    feed_signal(session,request,sizeof(request));
}

static void test_dual_hid_channel_setup_and_keyboard_input(void) {
    struct nexus_bt_classic_hid_session session = {0};
    assert(nexus_bt_classic_hid_start(&session));
    const uint8_t control_request[] = {
        0x02U,2U,4U,0U,0x11U,0U,0x40U,0U
    };
    const uint8_t interrupt_request[] = {
        0x02U,3U,4U,0U,0x13U,0U,0x41U,0U
    };
    pop_and_ack(&session,1U,control_request,sizeof(control_request));
    pop_and_ack(&session,1U,interrupt_request,sizeof(interrupt_request));

    feed_connection_response(&session,2U,0x50U,0x40U);
    feed_connection_response(&session,3U,0x51U,0x41U);
    const uint8_t control_config_request[] = {
        0x04U,4U,4U,0U,0x50U,0U,0U,0U
    };
    const uint8_t interrupt_config_request[] = {
        0x04U,5U,4U,0U,0x51U,0U,0U,0U
    };
    pop_and_ack(&session,1U,control_config_request,
                sizeof(control_config_request));
    pop_and_ack(&session,1U,interrupt_config_request,
                sizeof(interrupt_config_request));

    feed_peer_config_request(&session,10U,0x40U);
    const uint8_t control_config_response[] = {
        0x05U,10U,6U,0U,0x40U,0U,0U,0U,0U,0U
    };
    pop_and_ack(&session,1U,control_config_response,
                sizeof(control_config_response));
    feed_peer_config_request(&session,11U,0x41U);
    const uint8_t interrupt_config_response[] = {
        0x05U,11U,6U,0U,0x41U,0U,0U,0U,0U,0U
    };
    pop_and_ack(&session,1U,interrupt_config_response,
                sizeof(interrupt_config_response));

    feed_config_response(&session,4U,0x40U);
    feed_config_response(&session,5U,0x41U);
    assert(session.state == NEXUS_BT_CLASSIC_HID_PROTOCOL_PENDING);
    const uint8_t set_protocol[] = {0x70U,0x00U};
    pop_and_ack(&session,0x50U,set_protocol,sizeof(set_protocol));

    const uint8_t handshake_pdu[] = {1U,0U,0x40U,0U,0x00U};
    assert(nexus_bt_classic_hid_handle_l2cap(
        &session,handshake_pdu,sizeof(handshake_pdu),NULL));
    assert(nexus_bt_classic_hid_is_ready(&session));

    const uint8_t keyboard_hidp[] = {
        0xA1U,0x02U,0U,0x04U,0U,0U,0U,0U,0U
    };
    uint8_t interrupt_l2cap[sizeof(keyboard_hidp) + 4U] = {
        sizeof(keyboard_hidp),0U,0x41U,0U
    };
    memcpy(interrupt_l2cap + 4U,keyboard_hidp,sizeof(keyboard_hidp));
    struct nexus_bt_classic_hid_report report = {0};
    assert(nexus_bt_classic_hid_handle_l2cap(
        &session,interrupt_l2cap,sizeof(interrupt_l2cap),&report));
    assert(report.valid && report.keyboard && !report.mouse);
    assert(report.keyboard_report[0] == 0x02U);
    assert(report.keyboard_report[2] == 0x04U);
}

static void test_mouse_input_and_failed_send_retry(void) {
    struct nexus_bt_classic_hid_session session = {0};
    assert(nexus_bt_classic_hid_start(&session));
    uint16_t channel = 0U;
    uint8_t payload[32] = {0U};
    size_t length = 0U;
    assert(nexus_bt_classic_hid_next_tx(
        &session,&channel,payload,sizeof(payload),&length));
    nexus_bt_classic_hid_tx_complete(&session,false);
    assert(nexus_bt_classic_hid_next_tx(
        &session,&channel,payload,sizeof(payload),&length));
    assert(channel == 1U && payload[0] == 0x02U);
    nexus_bt_classic_hid_tx_complete(&session,true);

    session.control.remote_cid = 0x50U;
    session.interrupt.remote_cid = 0x51U;
    session.control.connected = true;
    session.interrupt.connected = true;
    session.control.local_configured = true;
    session.control.peer_configured = true;
    session.interrupt.local_configured = true;
    session.interrupt.peer_configured = true;
    session.protocol_set_sent = true;
    session.protocol_accepted = true;
    session.state = NEXUS_BT_CLASSIC_HID_READY;
    const uint8_t mouse_hidp[] = {0xA1U,0x05U,0xFEU,0x03U};
    const uint8_t l2cap[] = {4U,0U,0x41U,0U,0xA1U,0x05U,0xFEU,0x03U};
    struct nexus_bt_classic_hid_report report = {0};
    assert(nexus_bt_classic_hid_handle_l2cap(
        &session,l2cap,sizeof(l2cap),&report));
    assert(report.valid && report.mouse && !report.keyboard);
    assert(report.mouse_event.buttons == 0x05U);
    assert(report.mouse_event.dx == -2);
    assert(report.mouse_event.dy == -3);
    (void)mouse_hidp;
}

static void test_bad_signaling_is_rejected(void) {
    struct nexus_bt_classic_hid_session session = {0};
    assert(nexus_bt_classic_hid_start(&session));
    const uint8_t pdu[] = {5U,0U,1U,0U,0x03U,1U,8U,0U,0U};
    assert(!nexus_bt_classic_hid_handle_l2cap(
        &session,pdu,sizeof(pdu),NULL));
}

int main(void) {
    test_dual_hid_channel_setup_and_keyboard_input();
    test_mouse_input_and_failed_send_retry();
    test_bad_signaling_is_rejected();
    puts("NEXUS CLASSIC BLUETOOTH HID L2CAP AND HIDP TESTS PASSED");
    return 0;
}
