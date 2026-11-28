#include "bluetooth_hogp.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool next(struct nexus_bt_hogp_client *client,
                 uint8_t *pdu, size_t *length) {
    return nexus_bt_hogp_client_next_request(
        client, pdu, 64U, length);
}

static void respond(struct nexus_bt_hogp_client *client,
                    const uint8_t *pdu, size_t length) {
    assert(nexus_bt_hogp_client_handle_response(client,pdu,length));
}

static void test_complete_hogp_discovery_and_notification_setup(void) {
    struct nexus_bt_hogp_client client = {0};
    uint8_t pdu[64] = {0U};
    size_t length = 0U;
    assert(nexus_bt_hogp_client_start(&client,247U));

    assert(next(&client,pdu,&length));
    assert(length == 3U && pdu[0] == 0x02U &&
           pdu[1] == 247U && pdu[2] == 0U);
    assert(!next(&client,pdu,&length));
    const uint8_t mtu_response[] = {0x03U,0x17U,0x00U};
    respond(&client,mtu_response,sizeof(mtu_response));

    assert(next(&client,pdu,&length));
    assert(length == 7U && pdu[0] == 0x10U &&
           pdu[1] == 1U && pdu[2] == 0U);
    const uint8_t service_response[] = {
        0x11U,0x06U,1U,0U,16U,0U,0x12U,0x18U
    };
    respond(&client,service_response,sizeof(service_response));

    assert(next(&client,pdu,&length));
    assert(pdu[0] == 0x08U && pdu[1] == 1U &&
           pdu[3] == 16U && pdu[5] == 3U && pdu[6] == 0x28U);
    const uint8_t characteristics[] = {
        0x09U,0x07U,
        0x02U,0x00U,0x02U,0x03U,0x00U,0x4AU,0x2AU,
        0x04U,0x00U,0x02U,0x05U,0x00U,0x4BU,0x2AU,
        0x06U,0x00U,0x04U,0x07U,0x00U,0x4CU,0x2AU,
        0x08U,0x00U,0x0CU,0x09U,0x00U,0x4EU,0x2AU,
        0x0AU,0x00U,0x12U,0x0BU,0x00U,0x4DU,0x2AU
    };
    respond(&client,characteristics,sizeof(characteristics));

    assert(next(&client,pdu,&length));
    assert(pdu[0] == 0x08U && pdu[1] == 11U);
    const uint8_t not_found_characteristics[] = {
        0x01U,0x08U,0x11U,0x00U,0x0AU
    };
    respond(&client,not_found_characteristics,
            sizeof(not_found_characteristics));

    assert(next(&client,pdu,&length));
    assert(length == 3U && pdu[0] == 0x0AU &&
           pdu[1] == 5U && pdu[2] == 0U);
    const uint8_t map_response[] = {
        0x0BU,0x05U,0x01U,0x09U,0x06U,0xA1U,0x01U
    };
    respond(&client,map_response,sizeof(map_response));
    const uint8_t *map = NULL;
    size_t map_length = 0U;
    assert(nexus_bt_hogp_client_get_report_map(
        &client,&map,&map_length));
    assert(map_length == sizeof(map_response)-1U && map[0] == 0x05U);

    assert(next(&client,pdu,&length));
    assert(length == 5U && pdu[0] == 0x04U &&
           pdu[1] == 12U && pdu[3] == 16U);
    const uint8_t descriptors[] = {
        0x05U,0x01U,
        0x0CU,0x00U,0x02U,0x29U,
        0x0DU,0x00U,0x08U,0x29U
    };
    respond(&client,descriptors,sizeof(descriptors));

    assert(next(&client,pdu,&length));
    assert(length == 3U && pdu[0] == 0x0AU &&
           pdu[1] == 13U && pdu[2] == 0U);
    const uint8_t report_reference[] = {0x0BU,0x01U,0x01U};
    respond(&client,report_reference,sizeof(report_reference));

    assert(next(&client,pdu,&length));
    assert(length == 5U && pdu[0] == 0x12U &&
           pdu[1] == 12U && pdu[2] == 0U &&
           pdu[3] == 1U && pdu[4] == 0U);
    const uint8_t write_response[] = {0x13U};
    respond(&client,write_response,sizeof(write_response));
    assert(nexus_bt_hogp_client_is_ready(&client));
    uint8_t report_id = 0U;
    assert(nexus_bt_hogp_client_find_input_report(&client,11U,&report_id));
    assert(report_id == 1U);
}

static void test_blob_read_paging(void) {
    struct nexus_bt_hogp_client client = {0};
    uint8_t request[64] = {0U};
    size_t length = 0U;
    assert(nexus_bt_hogp_client_start(&client,23U));
    assert(next(&client,request,&length));
    const uint8_t mtu[] = {0x03U,0x17U,0U};
    respond(&client,mtu,sizeof(mtu));
    assert(next(&client,request,&length));
    const uint8_t service[] = {0x11U,6U,1U,0U,10U,0U,0x12U,0x18U};
    respond(&client,service,sizeof(service));
    assert(next(&client,request,&length));
    const uint8_t characteristics[] = {
        0x09U,7U, 2U,0U,2U,3U,0U,0x4BU,0x2AU
    };
    respond(&client,characteristics,sizeof(characteristics));
    assert(next(&client,request,&length));
    const uint8_t no_more[] = {0x01U,0x08U,4U,0U,0x0AU};
    respond(&client,no_more,sizeof(no_more));
    assert(next(&client,request,&length));
    const uint8_t first_chunk[] = {
        0x0BU,1U,2U,3U,4U,5U,6U,7U,8U,9U,10U,11U,
        12U,13U,14U,15U,16U,17U,18U,19U,20U,21U
    };
    respond(&client,first_chunk,sizeof(first_chunk));
    assert(next(&client,request,&length));
    assert(length == 5U && request[0] == 0x0CU &&
           request[1] == 3U && request[2] == 0U &&
           request[3] == 21U && request[4] == 0U);
    const uint8_t final_chunk[] = {0x0DU,22U,23U,24U};
    respond(&client,final_chunk,sizeof(final_chunk));
    const uint8_t *map = NULL;
    size_t map_length = 0U;
    assert(nexus_bt_hogp_client_get_report_map(
        &client,&map,&map_length));
    assert(map_length == 24U && map[0] == 1U && map[23] == 24U);
}

static void test_invalid_and_unmatched_att_responses_fail_closed(void) {
    struct nexus_bt_hogp_client client = {0};
    uint8_t pdu[8] = {0U};
    size_t length = 0U;
    assert(!nexus_bt_hogp_client_start(&client,22U));
    assert(nexus_bt_hogp_client_start(&client,64U));
    assert(next(&client,pdu,&length));
    const uint8_t wrong_opcode[] = {0x0BU,1U};
    assert(!nexus_bt_hogp_client_handle_response(
        &client,wrong_opcode,sizeof(wrong_opcode)));
    assert(client.state == NEXUS_BT_HOGP_FAILED);
}

int main(void) {
    test_complete_hogp_discovery_and_notification_setup();
    test_blob_read_paging();
    test_invalid_and_unmatched_att_responses_fail_closed();
    puts("NEXUS BLE HOGP CLIENT STATE MACHINE TESTS PASSED");
    return 0;
}
