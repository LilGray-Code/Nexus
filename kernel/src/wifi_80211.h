#ifndef NEXUS_WIFI_80211_H
#define NEXUS_WIFI_80211_H

#include "wifi.h"

/* Parse a received beacon or probe-response management frame into a bounded
 * scan record. No authentication or association is performed here.
 */
bool wifi_parse_beacon_frame(const uint8_t *frame, size_t frame_length,
                             int8_t signal_dbm,
                             struct nexus_wifi_network *network);

#endif
