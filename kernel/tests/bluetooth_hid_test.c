#include "bluetooth_hid.h"

#include <assert.h>
#include <stdio.h>

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
    puts("NEXUS BLUETOOTH L2CAP HIDP AND HOGP REPORT PARSER TESTS PASSED");
    return 0;
}
