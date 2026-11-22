#include "wifi_80211.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static size_t make_frame(uint8_t *frame, uint16_t subtype,
                         bool include_rsn, uint8_t akm_type) {
    memset(frame, 0, 256U);
    frame[0] = (uint8_t)(subtype & 0xFFU);
    frame[1] = (uint8_t)(subtype >> 8);
    const uint8_t bssid[6] = {0x02,0x11,0x22,0x33,0x44,0x55};
    memcpy(frame + 16U, bssid, sizeof(bssid));
    size_t n = 36U;
    frame[n++] = 0; frame[n++] = 4;
    memcpy(frame+n, "NEXU", 4U); n += 4U;
    frame[n++] = 3; frame[n++] = 1; frame[n++] = 6;
    if (include_rsn) {
        frame[n++] = 48; frame[n++] = 20;
        frame[n++] = 1; frame[n++] = 0; /* RSN version */
        frame[n++] = 0; frame[n++] = 0x0F; frame[n++] = 0xAC;
        frame[n++] = 4; /* CCMP group cipher */
        frame[n++] = 1; frame[n++] = 0; /* one pairwise cipher */
        frame[n++] = 0; frame[n++] = 0x0F; frame[n++] = 0xAC;
        frame[n++] = 4; /* CCMP pairwise */
        frame[n++] = 1; frame[n++] = 0; /* one AKM */
        frame[n++] = 0; frame[n++] = 0x0F; frame[n++] = 0xAC;
        frame[n++] = akm_type;
    }
    return n;
}

int main(void) {
    uint8_t frame[256];
    struct nexus_wifi_network network;
    size_t n = make_frame(frame, 0x0080U, false, 0U);
    assert(wifi_parse_beacon_frame(frame,n,-42,&network));
    assert(network.ssid_length==4U && memcmp(network.ssid,"NEXU",4U)==0);
    assert(network.channel==6U && network.signal_dbm==-42);
    assert(network.security==NEXUS_WIFI_SECURITY_OPEN);
    assert(network.bssid[0]==0x02U && network.bssid[5]==0x55U);

    n = make_frame(frame,0x0050U,true,2U);
    assert(wifi_parse_beacon_frame(frame,n,-61,&network));
    assert(network.security==NEXUS_WIFI_SECURITY_WPA2_PSK);

    n = make_frame(frame,0x0080U,true,8U);
    assert(wifi_parse_beacon_frame(frame,n,-30,&network));
    assert(network.security==NEXUS_WIFI_SECURITY_WPA3_SAE);

    assert(!wifi_parse_beacon_frame(frame,10U,-30,&network));
    frame[0]=0x08U; /* Data frame, not a management beacon. */
    assert(!wifi_parse_beacon_frame(frame,n,-30,&network));

    n = make_frame(frame,0x0080U,false,0U);
    frame[37]=250U; /* Invalid DS channel. */
    assert(!wifi_parse_beacon_frame(frame,n,-30,&network));

    n = make_frame(frame,0x0080U,true,2U);
    frame[n-1U]=99U; /* Unknown AKM suite. */
    assert(!wifi_parse_beacon_frame(frame,n,-30,&network));

    n = make_frame(frame,0x0080U,false,0U);
    frame[n-1U]=0xFFU; /* Truncated trailing information element. */
    assert(!wifi_parse_beacon_frame(frame,n,-30,&network));
    return 0;
}
