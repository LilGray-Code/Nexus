#include "bluetooth_smp.h"
#include "aes_ccm.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_core_legacy_crypto_vectors(void) {
    const uint8_t tk[16] = {0U};
    const uint8_t random[16] = {
        0x57U,0x83U,0xD5U,0x21U,0x56U,0xADU,0x6FU,0x0E,
        0x63U,0x88U,0x27U,0x4E,0xC6U,0x70U,0x2EU,0xE0U
    };
    const uint8_t preq[7] = {
        0x07U,0x07U,0x10U,0x00U,0x00U,0x01U,0x01U
    };
    const uint8_t pres[7] = {
        0x05U,0x00U,0x08U,0x00U,0x00U,0x03U,0x02U
    };
    const uint8_t ia[6] = {0xA1U,0xA2U,0xA3U,0xA4U,0xA5U,0xA6U};
    const uint8_t ra[6] = {0xB1U,0xB2U,0xB3U,0xB4U,0xB5U,0xB6U};
    const uint8_t expected_confirm[16] = {
        0x1EU,0x1EU,0x3FU,0xEFU,0x87U,0x89U,0x88U,0xEAU,
        0xD2U,0xA7U,0x4DU,0xC5U,0xBEU,0xF1U,0x3BU,0x86U
    };
    uint8_t confirm[16] = {0U};
    assert(nexus_bt_smp_legacy_confirm(tk,random,preq,pres,1U,0U,
        ia,ra,confirm));
    assert(memcmp(confirm,expected_confirm,sizeof(confirm)) == 0);

    const uint8_t r1[16] = {
        0x00U,0x0FU,0x0EU,0x0DU,0x0CU,0x0BU,0x0AU,0x09U,
        0x11U,0x22U,0x33U,0x44U,0x55U,0x66U,0x77U,0x88U
    };
    const uint8_t r2[16] = {
        0x01U,0x02U,0x03U,0x04U,0x05U,0x06U,0x07U,0x08U,
        0x99U,0xAAU,0xBBU,0xCCU,0xDDU,0xEEU,0xFFU,0x00U
    };
    const uint8_t expected_stk[16] = {
        0x9AU,0x1FU,0xE1U,0xF0U,0xE8U,0xB0U,0xF4U,0x9BU,
        0x5BU,0x42U,0x16U,0xAEU,0x79U,0x6DU,0xA0U,0x62U
    };
    uint8_t stk[16] = {0U};
    assert(nexus_bt_smp_legacy_stk(tk,r1,r2,stk));
    assert(memcmp(stk,expected_stk,sizeof(stk)) == 0);
}

static void test_legacy_just_works_pairing_transcript(void) {
    const uint8_t local_address[6] = {1U,2U,3U,4U,5U,6U};
    const uint8_t peer_address[6] = {7U,8U,9U,10U,11U,12U};
    const uint8_t local_random[16] = {
        0x10U,0x11U,0x12U,0x13U,0x14U,0x15U,0x16U,0x17U,
        0x18U,0x19U,0x1AU,0x1BU,0x1CU,0x1DU,0x1EU,0x1FU
    };
    const uint8_t peer_random[16] = {
        0x20U,0x21U,0x22U,0x23U,0x24U,0x25U,0x26U,0x27U,
        0x28U,0x29U,0x2AU,0x2BU,0x2CU,0x2DU,0x2EU,0x2FU
    };
    struct nexus_bt_smp_session session = {0};
    uint8_t out[32] = {0U};
    size_t out_length = 0U;
    assert(nexus_bt_smp_start_with_random(&session,0U,local_address,
        1U,peer_address,local_random,out,sizeof(out),&out_length));
    assert(out_length == 7U && out[0] == NEXUS_BT_SMP_PAIRING_REQUEST);
    assert(out[1] == 3U && out[3] == 0U && out[4] == 16U);
    assert(session.state == NEXUS_BT_SMP_WAIT_PAIRING_RESPONSE);

    const uint8_t response[7] = {
        NEXUS_BT_SMP_PAIRING_RESPONSE,3U,0U,0U,16U,0U,0U
    };
    assert(nexus_bt_smp_handle_pdu(&session,response,sizeof(response),
        out,sizeof(out),&out_length,(bool[1]){false}));
    assert(out_length == 17U && out[0] == NEXUS_BT_SMP_PAIRING_CONFIRM);
    uint8_t peer_confirm[17] = {NEXUS_BT_SMP_PAIRING_CONFIRM};
    assert(nexus_bt_smp_legacy_confirm(session.tk,peer_random,
        session.preq,session.pres,0U,1U,local_address,peer_address,
        peer_confirm+1U));
    bool start_encryption = false;
    assert(nexus_bt_smp_handle_pdu(&session,peer_confirm,sizeof(peer_confirm),
        out,sizeof(out),&out_length,&start_encryption));
    assert(out_length == 17U && out[0] == NEXUS_BT_SMP_PAIRING_RANDOM);
    assert(memcmp(out+1U,local_random,16U) == 0);
    uint8_t peer_random_pdu[17] = {NEXUS_BT_SMP_PAIRING_RANDOM};
    memcpy(peer_random_pdu+1U,peer_random,16U);
    assert(nexus_bt_smp_handle_pdu(&session,peer_random_pdu,
        sizeof(peer_random_pdu),out,sizeof(out),&out_length,&start_encryption));
    assert(start_encryption && out_length == 0U);
    assert(session.state == NEXUS_BT_SMP_ENCRYPTION_PENDING);
    uint8_t expected_stk[16] = {0U}, actual_stk[16] = {0U};
    assert(nexus_bt_smp_legacy_stk(session.tk,peer_random,local_random,
        expected_stk));
    assert(nexus_bt_smp_get_stk(&session,actual_stk));
    assert(memcmp(expected_stk,actual_stk,16U) == 0);
    assert(nexus_bt_smp_mark_encrypted(&session,true));
    assert(session.state == NEXUS_BT_SMP_ENCRYPTED);
    assert(!nexus_bt_smp_get_stk(&session,actual_stk));
}

static void test_reject_mitm_and_wrong_confirms(void) {
    const uint8_t local_address[6] = {1U,2U,3U,4U,5U,6U};
    const uint8_t peer_address[6] = {7U,8U,9U,10U,11U,12U};
    const uint8_t random[16] = {0U};
    struct nexus_bt_smp_session session = {0};
    uint8_t out[32] = {0U};
    size_t length = 0U;
    bool encrypt = false;
    assert(nexus_bt_smp_start_with_random(&session,0U,local_address,0U,
        peer_address,random,out,sizeof(out),&length));
    const uint8_t mitm[7] = {
        NEXUS_BT_SMP_PAIRING_RESPONSE,3U,0U,0x04U,16U,0U,0U
    };
    assert(nexus_bt_smp_handle_pdu(&session,mitm,sizeof(mitm),
        out,sizeof(out),&length,&encrypt));
    assert(length == 2U && out[0] == NEXUS_BT_SMP_PAIRING_FAILED);
    assert(out[1] == NEXUS_BT_SMP_REASON_AUTHENTICATION_REQUIREMENTS);
    assert(session.state == NEXUS_BT_SMP_FAILED);

    assert(nexus_bt_smp_start_with_random(&session,0U,local_address,0U,
        peer_address,random,out,sizeof(out),&length));
    const uint8_t response[7] = {
        NEXUS_BT_SMP_PAIRING_RESPONSE,3U,0U,0U,16U,0U,0U
    };
    assert(nexus_bt_smp_handle_pdu(&session,response,sizeof(response),
        out,sizeof(out),&length,&encrypt));
    uint8_t invalid_confirm[17] = {NEXUS_BT_SMP_PAIRING_CONFIRM};
    invalid_confirm[1] = 0xAAU;
    assert(nexus_bt_smp_handle_pdu(&session,invalid_confirm,
        sizeof(invalid_confirm),out,sizeof(out),&length,&encrypt));
    uint8_t invalid_random[17] = {NEXUS_BT_SMP_PAIRING_RANDOM};
    assert(nexus_bt_smp_handle_pdu(&session,invalid_random,
        sizeof(invalid_random),out,sizeof(out),&length,&encrypt));
    assert(!encrypt);
    assert(length == 2U && out[0] == NEXUS_BT_SMP_PAIRING_FAILED);
    assert(out[1] == NEXUS_BT_SMP_REASON_CONFIRM_VALUE_FAILED);
}

static void test_reject_reserved_auth_and_unknown_commands(void) {
    const uint8_t local[6] = {1U,2U,3U,4U,5U,6U};
    const uint8_t peer[6] = {7U,8U,9U,10U,11U,12U};
    const uint8_t random[16] = {0U};
    uint8_t out[32] = {0U};
    size_t length = 0U;
    bool encrypt = false;
    struct nexus_bt_smp_session session = {0};

    assert(nexus_bt_smp_start_with_random(&session,0U,local,0U,peer,
        random,out,sizeof(out),&length));
    const uint8_t reserved_auth[7] = {
        NEXUS_BT_SMP_PAIRING_RESPONSE,3U,0U,0x80U,16U,0U,0U
    };
    assert(nexus_bt_smp_handle_pdu(&session,reserved_auth,
        sizeof(reserved_auth),out,sizeof(out),&length,&encrypt));
    assert(length == 2U && out[0] == NEXUS_BT_SMP_PAIRING_FAILED);
    assert(out[1] == NEXUS_BT_SMP_REASON_INVALID_PARAMETERS);

    assert(nexus_bt_smp_start_with_random(&session,0U,local,0U,peer,
        random,out,sizeof(out),&length));
    const uint8_t bonding_request[7] = {
        NEXUS_BT_SMP_PAIRING_RESPONSE,3U,0U,0x01U,16U,0U,0U
    };
    assert(nexus_bt_smp_handle_pdu(&session,bonding_request,
        sizeof(bonding_request),out,sizeof(out),&length,&encrypt));
    assert(length == 2U && out[0] == NEXUS_BT_SMP_PAIRING_FAILED);
    assert(out[1] == NEXUS_BT_SMP_REASON_PAIRING_NOT_SUPPORTED);

    assert(nexus_bt_smp_start_with_random(&session,0U,local,0U,peer,
        random,out,sizeof(out),&length));
    const uint8_t unknown_command[] = {0x0FU};
    assert(nexus_bt_smp_handle_pdu(&session,unknown_command,
        sizeof(unknown_command),out,sizeof(out),&length,&encrypt));
    assert(length == 2U && out[0] == NEXUS_BT_SMP_PAIRING_FAILED);
    assert(out[1] == NEXUS_BT_SMP_REASON_COMMAND_NOT_SUPPORTED);
}

static void test_failed_encryption_wipes_temporary_secrets(void) {
    struct nexus_bt_smp_session session = {
        .state = NEXUS_BT_SMP_ENCRYPTION_PENDING
    };
    memset(session.tk,0x11U,sizeof(session.tk));
    memset(session.stk,0x22U,sizeof(session.stk));
    memset(session.local_random,0x33U,sizeof(session.local_random));
    memset(session.remote_random,0x44U,sizeof(session.remote_random));
    memset(session.local_confirm,0x55U,sizeof(session.local_confirm));
    memset(session.remote_confirm,0x66U,sizeof(session.remote_confirm));
    assert(nexus_bt_smp_mark_encrypted(&session,false));
    assert(session.state == NEXUS_BT_SMP_FAILED);
    assert(session.failure_reason == NEXUS_BT_SMP_REASON_UNSPECIFIED);
    const uint8_t *arrays[] = {
        session.tk,session.stk,session.local_random,
        session.remote_random,session.local_confirm,session.remote_confirm
    };
    const size_t sizes[] = {16U,16U,16U,16U,16U,16U};
    for (size_t i = 0U; i < sizeof(sizes)/sizeof(sizes[0]); ++i)
        for (size_t j = 0U; j < sizes[i]; ++j)
            assert(arrays[i][j] == 0U);
}

int main(void) {
    test_core_legacy_crypto_vectors();
    test_legacy_just_works_pairing_transcript();
    test_reject_mitm_and_wrong_confirms();
    test_reject_reserved_auth_and_unknown_commands();
    test_failed_encryption_wipes_temporary_secrets();
    puts("NEXUS BLE SMP LEGACY PAIRING TESTS PASSED");
    return 0;
}
