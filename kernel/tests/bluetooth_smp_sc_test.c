#include "bluetooth_smp_sc.h"
#include "bluetooth_sc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_complete_secure_connections_just_works_transcript(void) {
    uint8_t local_address[7] = {0U,1U,2U,3U,4U,5U,6U};
    uint8_t peer_address[7] = {1U,7U,8U,9U,10U,11U,12U};
    uint8_t local_public[64],peer_public[64];
    for (size_t i = 0U; i < 64U; ++i) {
        local_public[i] = (uint8_t)(i+1U);
        peer_public[i] = (uint8_t)(0x80U+i);
    }
    const uint8_t dhkey[32] = {
        0xECU,0x02U,0x34U,0xA3U,0x57U,0xC8U,0xADU,0x05U,
        0x34U,0x10U,0x10U,0xA6U,0x0AU,0x39U,0x7DU,0x9BU,
        0x99U,0x79U,0x6BU,0x13U,0xB4U,0xF8U,0x66U,0xF1U,
        0x86U,0x8DU,0x34U,0xF3U,0x73U,0xBFU,0xA6U,0x98U
    };
    const uint8_t local_nonce[16] = {
        0xD5U,0xCBU,0x84U,0x54U,0xD1U,0x77U,0x73U,0x3EU,
        0xFFU,0xFFU,0xB2U,0xECU,0x71U,0x2BU,0xAEU,0xABU
    };
    const uint8_t peer_nonce[16] = {
        0xA6U,0xE8U,0xE7U,0xCCU,0x25U,0xA7U,0x5FU,0x6EU,
        0x21U,0x65U,0x83U,0xF7U,0xFFU,0x3DU,0xC4U,0xCFU
    };

    struct nexus_bt_smp_sc_session session = {0};
    uint8_t out[80] = {0U};
    size_t out_length = 0U;
    assert(nexus_bt_smp_sc_start(&session,local_address,peer_address,
        local_public,out,sizeof(out),&out_length));
    assert(out_length == 7U && out[0] == NEXUS_BT_SMP_SC_PAIRING_REQUEST);
    assert(out[1] == 3U && out[2] == 0U && out[3] == 0x08U);
    assert(session.state == NEXUS_BT_SMP_SC_WAIT_PAIRING_RESPONSE);

    const uint8_t response[7] = {
        NEXUS_BT_SMP_SC_PAIRING_RESPONSE,3U,0U,0x08U,16U,0U,0U
    };
    bool generate_dhkey = false, start_encryption = false, fallback = false;
    assert(nexus_bt_smp_sc_handle_pdu(&session,response,sizeof(response),
        out,sizeof(out),&out_length,&generate_dhkey,&start_encryption,&fallback));
    assert(!generate_dhkey && !start_encryption && !fallback);
    assert(out_length == 65U &&
           out[0] == NEXUS_BT_SMP_SC_PAIRING_PUBLIC_KEY);
    assert(memcmp(out+1U,local_public,64U) == 0);
    assert(session.state == NEXUS_BT_SMP_SC_WAIT_PEER_PUBLIC_KEY);

    uint8_t public_pdu[65] = {NEXUS_BT_SMP_SC_PAIRING_PUBLIC_KEY};
    memcpy(public_pdu+1U,peer_public,64U);
    assert(nexus_bt_smp_sc_handle_pdu(&session,public_pdu,sizeof(public_pdu),
        out,sizeof(out),&out_length,&generate_dhkey,&start_encryption,&fallback));
    assert(generate_dhkey && session.state == NEXUS_BT_SMP_SC_DHKEY_PENDING);

    assert(nexus_bt_smp_sc_handle_dhkey(&session,dhkey,local_nonce,
        out,sizeof(out),&out_length));
    assert(out_length == 17U && out[0] == NEXUS_BT_SMP_SC_PAIRING_CONFIRM);
    uint8_t peer_confirm[17] = {NEXUS_BT_SMP_SC_PAIRING_CONFIRM};
    assert(nexus_bt_sc_f4(peer_public,local_public,peer_nonce,0U,
        peer_confirm+1U));
    assert(nexus_bt_smp_sc_handle_pdu(&session,peer_confirm,sizeof(peer_confirm),
        out,sizeof(out),&out_length,&generate_dhkey,&start_encryption,&fallback));
    assert(out_length == 17U && out[0] == NEXUS_BT_SMP_SC_PAIRING_RANDOM);
    assert(memcmp(out+1U,local_nonce,16U) == 0);

    uint8_t peer_random[17] = {NEXUS_BT_SMP_SC_PAIRING_RANDOM};
    memcpy(peer_random+1U,peer_nonce,16U);
    assert(nexus_bt_smp_sc_handle_pdu(&session,peer_random,sizeof(peer_random),
        out,sizeof(out),&out_length,&generate_dhkey,&start_encryption,&fallback));
    assert(out_length == 17U && out[0] == NEXUS_BT_SMP_SC_PAIRING_DHKEY_CHECK);
    const uint8_t zero_r[16] = {0U};
    uint8_t expected_peer_check[16] = {0U};
    const uint8_t peer_io[3] = {0x08U,0U,3U};
    assert(nexus_bt_sc_f6(session.mac_key,peer_nonce,local_nonce,zero_r,
        peer_io,peer_address,local_address,expected_peer_check));
    uint8_t peer_dhkey_check[17] = {NEXUS_BT_SMP_SC_PAIRING_DHKEY_CHECK};
    memcpy(peer_dhkey_check+1U,expected_peer_check,16U);
    assert(nexus_bt_smp_sc_handle_pdu(&session,peer_dhkey_check,
        sizeof(peer_dhkey_check),out,sizeof(out),&out_length,
        &generate_dhkey,&start_encryption,&fallback));
    assert(start_encryption && !generate_dhkey && !fallback);
    assert(session.state == NEXUS_BT_SMP_SC_ENCRYPTION_PENDING);
    uint8_t ltk[16] = {0U};
    assert(nexus_bt_smp_sc_get_ltk(&session,ltk));
    assert(memcmp(ltk,session.ltk,16U) == 0);
    assert(nexus_bt_smp_sc_mark_encrypted(&session,true));
    assert(session.state == NEXUS_BT_SMP_SC_ENCRYPTED);
    assert(!nexus_bt_smp_sc_get_ltk(&session,ltk));
}

static void test_legacy_fallback_and_security_rejections(void) {
    uint8_t local_address[7] = {0U,1U,2U,3U,4U,5U,6U};
    uint8_t peer_address[7] = {0U,7U,8U,9U,10U,11U,12U};
    uint8_t local_public[64];
    for (size_t i = 0U; i < 64U; ++i) local_public[i]=(uint8_t)(i+1U);
    struct nexus_bt_smp_sc_session session = {0};
    uint8_t out[80] = {0U}; size_t length = 0U;
    bool dh=false,encrypt=false,fallback=false;
    assert(nexus_bt_smp_sc_start(&session,local_address,peer_address,
        local_public,out,sizeof(out),&length));
    const uint8_t legacy_response[7] = {
        NEXUS_BT_SMP_SC_PAIRING_RESPONSE,3U,0U,0U,16U,0U,0U
    };
    assert(nexus_bt_smp_sc_handle_pdu(&session,legacy_response,
        sizeof(legacy_response),out,sizeof(out),&length,&dh,&encrypt,&fallback));
    assert(fallback && session.state == NEXUS_BT_SMP_SC_FALLBACK_LEGACY);

    assert(nexus_bt_smp_sc_start(&session,local_address,peer_address,
        local_public,out,sizeof(out),&length));
    const uint8_t mitm_response[7] = {
        NEXUS_BT_SMP_SC_PAIRING_RESPONSE,3U,0U,0x0CU,16U,0U,0U
    };
    assert(nexus_bt_smp_sc_handle_pdu(&session,mitm_response,
        sizeof(mitm_response),out,sizeof(out),&length,&dh,&encrypt,&fallback));
    assert(!fallback && session.state == NEXUS_BT_SMP_SC_FAILED);
    assert(length == 2U && out[0] == NEXUS_BT_SMP_SC_PAIRING_FAILED);

    assert(nexus_bt_smp_sc_start(&session,local_address,peer_address,
        local_public,out,sizeof(out),&length));
    const uint8_t sc_response[7] = {
        NEXUS_BT_SMP_SC_PAIRING_RESPONSE,3U,0U,0x08U,16U,0U,0U
    };
    assert(nexus_bt_smp_sc_handle_pdu(&session,sc_response,
        sizeof(sc_response),out,sizeof(out),&length,&dh,&encrypt,&fallback));
    uint8_t invalid_pub[65] = {NEXUS_BT_SMP_SC_PAIRING_PUBLIC_KEY};
    memset(invalid_pub+1U,0U,64U);
    assert(nexus_bt_smp_sc_handle_pdu(&session,invalid_pub,
        sizeof(invalid_pub),out,sizeof(out),&length,&dh,&encrypt,&fallback));
    assert(session.state == NEXUS_BT_SMP_SC_FAILED);
    assert(length == 2U && out[1] == 0x0BU);
}

int main(void) {
    test_complete_secure_connections_just_works_transcript();
    test_legacy_fallback_and_security_rejections();
    puts("NEXUS BLE SMP SECURE CONNECTIONS STATE MACHINE TESTS PASSED");
    return 0;
}
