#include "wifi_80211.h"

#include <stddef.h>
#include <stdint.h>

#define IEEE80211_HEADER_SIZE 24U
#define BEACON_FIXED_SIZE 12U
#define IEEE80211_TYPE_MASK 0x000CU
#define IEEE80211_SUBTYPE_MASK 0x00F0U
#define IEEE80211_TYPE_MANAGEMENT 0x0000U
#define IEEE80211_SUBTYPE_BEACON 0x0080U
#define IEEE80211_SUBTYPE_PROBE_RESPONSE 0x0050U
#define IE_SSID 0U
#define IE_DS_PARAMETER_SET 3U
#define IE_RSN 48U
#define IE_HT_OPERATION 61U
#define IE_VENDOR 221U

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static bool suite_is(const uint8_t *suite, uint8_t type) {
    return suite[0] == 0x00U && suite[1] == 0x0FU &&
           suite[2] == 0xACU && suite[3] == type;
}

static bool parse_rsn(const uint8_t *data, size_t length,
                      enum nexus_wifi_security *security) {
    if (data == NULL || security == NULL || length < 2U ||
        read_le16(data) != 1U) return false;
    size_t offset = 2U;
    /* This parser only advertises networks the planned initial radio path
     * can protect: CCMP-128 group and pairwise encryption. */
    if (length - offset < 6U || data[offset] != 0x00U ||
        data[offset + 1U] != 0x0FU || data[offset + 2U] != 0xACU ||
        data[offset + 3U] != 4U) return false;
    offset += 4U; /* CCMP-128 group cipher suite. */

    if (length - offset < 2U) return false;
    const uint16_t pairwise_count = read_le16(data + offset);
    offset += 2U;
    if (pairwise_count == 0U ||
        (size_t)pairwise_count > (length - offset) / 4U) return false;
    for (uint16_t i = 0U; i < pairwise_count; ++i) {
        const uint8_t *suite = data + offset + (size_t)i * 4U;
        if (!suite_is(suite, 4U)) return false;
    }
    offset += (size_t)pairwise_count * 4U;

    if (length - offset < 2U) return false;
    const uint16_t akm_count = read_le16(data + offset);
    offset += 2U;
    if (akm_count == 0U ||
        (size_t)akm_count > (length - offset) / 4U) return false;

    bool has_psk = false;
    bool has_sae = false;
    for (uint16_t i = 0U; i < akm_count; ++i) {
        const uint8_t *suite = data + offset + (size_t)i * 4U;
        if (suite_is(suite, 2U)) has_psk = true;
        if (suite_is(suite, 8U)) has_sae = true;
    }
    offset += (size_t)akm_count * 4U;

    /* Optional RSN capabilities, PMKID list and group-management cipher. */
    uint16_t capabilities = 0U;
    if (offset < length) {
        if (length - offset < 2U) return false;
        capabilities = read_le16(data + offset);
        offset += 2U;
        if (offset < length) {
            if (length - offset < 2U) return false;
            const uint16_t pmkid_count = read_le16(data + offset);
            offset += 2U;
            if ((size_t)pmkid_count > (length - offset) / 16U)
                return false;
            offset += (size_t)pmkid_count * 16U;
            if (offset < length) {
                if (length - offset != 4U) return false;
                offset += 4U;
            }
        }
    }
    if (offset != length || (!has_psk && !has_sae)) return false;
    /* WPA3-Personal requires both MFPC and MFPR. Do not label an SAE
     * advertisement usable when management-frame protection is absent. */
    if (has_sae && (capabilities & 0x00C0U) != 0x00C0U) return false;
    *security = has_sae ? NEXUS_WIFI_SECURITY_WPA3_SAE
                        : NEXUS_WIFI_SECURITY_WPA2_PSK;
    return true;
}

bool wifi_parse_beacon_frame(const uint8_t *frame, size_t frame_length,
                             int8_t signal_dbm,
                             struct nexus_wifi_network *network) {
    if (frame == NULL || network == NULL ||
        frame_length < IEEE80211_HEADER_SIZE + BEACON_FIXED_SIZE) return false;

    const uint16_t frame_control = read_le16(frame);
    const uint16_t subtype = frame_control & IEEE80211_SUBTYPE_MASK;
    if ((frame_control & 3U) != 0U ||
        (frame_control & IEEE80211_TYPE_MASK) != IEEE80211_TYPE_MANAGEMENT ||
        (subtype != IEEE80211_SUBTYPE_BEACON &&
         subtype != IEEE80211_SUBTYPE_PROBE_RESPONSE)) return false;

    struct nexus_wifi_network parsed = {0};
    for (size_t i = 0U; i < 6U; ++i) parsed.bssid[i] = frame[16U + i];
    parsed.signal_dbm = signal_dbm;

    size_t offset = IEEE80211_HEADER_SIZE + BEACON_FIXED_SIZE;
    bool have_ssid = false;
    bool have_channel = false;
    bool have_rsn = false;
    bool unsupported_wpa1 = false;
    while (offset < frame_length) {
        if (frame_length - offset < 2U) return false;
        const uint8_t id = frame[offset];
        const size_t length = frame[offset + 1U];
        offset += 2U;
        if (length > frame_length - offset) return false;
        const uint8_t *value = frame + offset;

        if (id == IE_SSID) {
            if (have_ssid || length == 0U ||
                length > NEXUS_WIFI_SSID_MAX) return false;
            for (size_t i = 0U; i < length; ++i)
                parsed.ssid[i] = value[i];
            parsed.ssid_length = (uint8_t)length;
            have_ssid = true;
        } else if ((id == IE_DS_PARAMETER_SET ||
                    id == IE_HT_OPERATION) && length >= 1U &&
                   !have_channel) {
            parsed.channel = value[0];
            have_channel = parsed.channel != 0U;
        } else if (id == IE_RSN) {
            if (have_rsn ||
                !parse_rsn(value, length, &parsed.security)) return false;
            have_rsn = true;
        } else if (id == IE_VENDOR && length >= 4U &&
                   value[0] == 0x00U && value[1] == 0x50U &&
                   value[2] == 0xF2U && value[3] == 0x01U) {
            unsupported_wpa1 = true;
        }
        offset += length;
    }
    if (!have_ssid || !have_channel || unsupported_wpa1) return false;
    if (!have_rsn) parsed.security = NEXUS_WIFI_SECURITY_OPEN;
    if (parsed.channel > 233U) return false;
    *network = parsed;
    return true;
}
