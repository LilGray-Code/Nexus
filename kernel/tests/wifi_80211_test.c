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
        frame[n++] = 48; frame[n++] = (uint8_t)(akm_type == 8U ? 20U : 18U);
        frame[n++] = 1; frame[n++] = 0; /* RSN version */
        frame[n++] = 0; frame[n++] = 0x0F; frame[n++] = 0xAC;
        frame[n++] = 4; /* CCMP group cipher */
        frame[n++] = 1; frame[n++] = 0; /* one pairwise cipher */
        frame[n++] = 0; frame[n++] = 0x0F; frame[n++] = 0xAC;
        frame[n++] = 4; /* CCMP pairwise */
        frame[n++] = 1; frame[n++] = 0; /* one AKM */
        frame[n++] = 0; frame[n++] = 0x0F; frame[n++] = 0xAC;
        frame[n++] = akm_type;
        if (akm_type == 8U) {
            frame[n++] = 0xC0U; frame[n++] = 0x00U; /* MFPC | MFPR */
        }
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
    assert(network.band==NEXUS_WIFI_BAND_2GHZ);
    assert(network.frequency_mhz==2437U);
    assert(network.security==NEXUS_WIFI_SECURITY_OPEN);
    assert(network.bssid[0]==0x02U && network.bssid[5]==0x55U);

    n = make_frame(frame,0x0080U,false,0U);
    frame[34] = 0x10U; /* Privacy capability without a supported RSN IE. */
    assert(!wifi_parse_beacon_frame(frame,n,-42,&network));

    n = make_frame(frame,0x0080U,false,0U);
    frame[44]=36U; /* 5 GHz channel 36. */
    assert(wifi_parse_beacon_frame(frame,n,-48,&network));
    assert(network.band==NEXUS_WIFI_BAND_5GHZ);
    assert(network.frequency_mhz==5180U);

    n = make_frame(frame,0x0050U,true,2U);
    assert(wifi_parse_beacon_frame(frame,n,-61,&network));
    assert(network.security==NEXUS_WIFI_SECURITY_WPA2_PSK);

    n = make_frame(frame,0x0080U,true,8U);
    assert(wifi_parse_beacon_frame(frame,n,-30,&network));
    assert(network.security==NEXUS_WIFI_SECURITY_WPA3_SAE);

    n = make_frame(frame,0x0080U,true,8U);
    frame[46] = 18U; /* SAE without required PMF capabilities. */
    assert(!wifi_parse_beacon_frame(frame,n-2U,-30,&network));

    n = make_frame(frame,0x0080U,true,2U);
    frame[52] = 2U; /* TKIP group cipher is outside supported profile. */
    assert(!wifi_parse_beacon_frame(frame,n,-30,&network));

    n = make_frame(frame,0x0080U,true,2U);
    frame[58] = 2U; /* TKIP pairwise cipher is outside supported profile. */
    assert(!wifi_parse_beacon_frame(frame,n,-30,&network));

    assert(!wifi_parse_beacon_frame(frame,10U,-30,&network));
    frame[0]=0x08U; /* Data frame, not a management beacon. */
    assert(!wifi_parse_beacon_frame(frame,n,-30,&network));

    n = make_frame(frame,0x0080U,false,0U);
    frame[44]=250U; /* Invalid DS channel. */
    assert(!wifi_parse_beacon_frame(frame,n,-30,&network));

    n = make_frame(frame,0x0080U,true,2U);
    frame[n-1U]=99U; /* Unknown AKM suite. */
    assert(!wifi_parse_beacon_frame(frame,n,-30,&network));

    n = make_frame(frame,0x0080U,false,0U);
    frame[n]=0U; /* One-byte trailing IE header is truncated. */
    assert(!wifi_parse_beacon_frame(frame,n+1U,-30,&network));

    n = make_frame(frame,0x0080U,false,0U);
    frame[n++]=221U; frame[n++]=4U;
    frame[n++]=0x00U; frame[n++]=0x50U; frame[n++]=0xF2U; frame[n++]=1U;
    assert(!wifi_parse_beacon_frame(frame,n,-30,&network)); /* WPA1 is not open. */
    return 0;
}
