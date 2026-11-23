#include "wifi.h"
#include "netdev.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct mock_driver {
    unsigned starts, scans, connects, disconnects, polls;
    bool fail_scan;
    bool fail_connect;
    unsigned frame_sends;
    unsigned frame_receives;
    size_t last_frame_length;
    size_t next_receive_length;
};

static bool mock_start(void *context) {
    ((struct mock_driver *)context)->starts++;
    return true;
}
static bool mock_scan(void *context) {
    struct mock_driver *m = context;
    m->scans++;
    return !m->fail_scan;
}
static bool mock_connect(void *context, const struct nexus_wifi_network *n,
                         const char *passphrase) {
    struct mock_driver *m = context;
    (void)n;
    (void)passphrase;
    m->connects++;
    return !m->fail_connect;
}
static void mock_disconnect(void *context) {
    ((struct mock_driver *)context)->disconnects++;
}
static void mock_poll(void *context) {
    ((struct mock_driver *)context)->polls++;
}


static bool mock_get_mac(void *context, uint8_t address[6]) {
    (void)context;
    static const uint8_t mac[6] = {0x02U,0x11U,0x22U,0x33U,0x44U,0x55U};
    memcpy(address, mac, sizeof(mac));
    return true;
}
static bool mock_send_frame(void *context, const void *frame, size_t length) {
    struct mock_driver *m = context;
    if (frame == NULL) return false;
    ++m->frame_sends;
    m->last_frame_length = length;
    return true;
}
static bool mock_receive_frame(void *context, void *buffer, size_t capacity,
                               size_t *length) {
    struct mock_driver *m = context;
    if (length == NULL) return false;
    *length = 0U;
    if (buffer == NULL || m->next_receive_length > capacity) return false;
    memset(buffer, 0xA5, m->next_receive_length);
    *length = m->next_receive_length;
    ++m->frame_receives;
    return true;
}

int main(void) {
    struct mock_driver mock = {0};
    const struct nexus_wifi_driver_ops incomplete_network_ops = {
        .start = mock_start,
        .scan = mock_scan,
        .connect = mock_connect,
        .disconnect = mock_disconnect,
        .poll = mock_poll,
        .get_mac = mock_get_mac
    };
    const struct nexus_wifi_driver_ops ops = {
        .start = mock_start,
        .scan = mock_scan,
        .connect = mock_connect,
        .disconnect = mock_disconnect,
        .poll = mock_poll,
        .get_mac = mock_get_mac,
        .send_frame = mock_send_frame,
        .receive_frame = mock_receive_frame
    };
    assert(!wifi_available());
    assert(wifi_get_state() == NEXUS_WIFI_UNAVAILABLE);
    assert(!wifi_scan());
    assert(netdev_device_count() == 0U);
    assert(!wifi_register_driver(&incomplete_network_ops, &mock));
    assert(!wifi_available());
    assert(wifi_register_driver(&ops, &mock));
    assert(wifi_network_adapter_registered());
    assert(netdev_device_count() == 1U);
    assert(netdev_default_index() == 0U);
    assert(strcmp(netdev_device_name_at(0U), "wlan0") == 0);
    uint8_t mac[6] = {0};
    assert(netdev_get_mac(mac));
    assert(mac[0] == 0x02U && mac[5] == 0x55U);
    uint8_t frame[64] = {0};
    assert(netdev_send_frame(frame, 14U));
    assert(mock.frame_sends == 1U && mock.last_frame_length == 14U);
    size_t received = 0U;
    mock.next_receive_length = 60U;
    assert(netdev_receive_frame(frame, sizeof(frame), &received));
    assert(received == 60U && frame[0] == 0xA5U);
    assert(mock.frame_receives == 1U);
    assert(!wifi_register_driver(&ops, &mock));
    assert(wifi_available());
    assert(wifi_get_state() == NEXUS_WIFI_READY);
    assert(wifi_start());
    assert(mock.starts == 1U);

    assert(wifi_scan());
    assert(wifi_get_state() == NEXUS_WIFI_SCANNING);
    assert(!wifi_scan());
    struct nexus_wifi_network networks[3] = {0};
    networks[0].ssid[0] = 'N'; networks[0].ssid[1] = 'X';
    networks[0].ssid_length = 2U;
    networks[0].channel = 6U;
    networks[0].signal_dbm = -66;
    networks[0].bssid[0] = 0x02U; networks[0].bssid[5] = 1U;
    networks[0].security = NEXUS_WIFI_SECURITY_WPA2_PSK;
    networks[1].ssid[0] = 'O';
    networks[1].ssid_length = 1U;
    networks[1].channel = 1U;
    networks[1].signal_dbm = -52;
    networks[1].bssid[0] = 0x02U; networks[1].bssid[5] = 2U;
    networks[1].security = NEXUS_WIFI_SECURITY_OPEN;
    networks[2] = networks[0];
    networks[2].signal_dbm = -38;
    assert(wifi_publish_scan_results(networks, 3U));
    assert(wifi_get_state() == NEXUS_WIFI_READY);
    assert(wifi_scan_result_count() == 2U);
    struct nexus_wifi_network copied = {0};
    assert(wifi_scan_result_at(0U, &copied));
    assert(copied.ssid_length == 2U && copied.channel == 6U);
    assert(copied.signal_dbm == -38 && copied.bssid[5] == 1U);
    assert(wifi_scan_result_at(1U, &copied));
    assert(copied.ssid_length == 1U && copied.signal_dbm == -52);
    assert(!wifi_scan_result_at(2U, &copied));

    assert(!wifi_connect(&networks[0], NULL));
    assert(!wifi_connect(&networks[0], "short"));
    assert(!wifi_connect(&networks[1], "not-empty"));
    networks[1].security = NEXUS_WIFI_SECURITY_WPA3_SAE;
    assert(wifi_connect(&networks[1], "s"));
    assert(wifi_get_state() == NEXUS_WIFI_CONNECTING);
    wifi_report_disconnected();
    assert(wifi_connect(&networks[0], "correcthorsebattery"));
    assert(wifi_get_state() == NEXUS_WIFI_CONNECTING);
    wifi_report_connected();
    assert(wifi_get_state() == NEXUS_WIFI_CONNECTED);
    wifi_disconnect();
    assert(mock.disconnects == 1U);
    assert(wifi_get_state() == NEXUS_WIFI_READY);

    assert(wifi_scan());
    struct nexus_wifi_network invalid = {0};
    invalid.ssid_length = 1U;
    assert(!wifi_publish_scan_results(&invalid, 1U));
    wifi_report_failure();
    assert(wifi_get_state() == NEXUS_WIFI_FAILED);
    wifi_poll();
    assert(mock.polls == 1U);
    return 0;
}
