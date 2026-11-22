#include "wifi.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

struct mock_driver {
    unsigned starts, scans, connects, disconnects, polls;
    bool fail_scan;
    bool fail_connect;
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

int main(void) {
    struct mock_driver mock = {0};
    const struct nexus_wifi_driver_ops ops = {
        mock_start, mock_scan, mock_connect, mock_disconnect, mock_poll
    };
    assert(!wifi_available());
    assert(wifi_get_state() == NEXUS_WIFI_UNAVAILABLE);
    assert(!wifi_scan());
    assert(wifi_register_driver(&ops, &mock));
    assert(!wifi_register_driver(&ops, &mock));
    assert(wifi_available());
    assert(wifi_get_state() == NEXUS_WIFI_READY);
    assert(wifi_start());
    assert(mock.starts == 1U);

    assert(wifi_scan());
    assert(wifi_get_state() == NEXUS_WIFI_SCANNING);
    assert(!wifi_scan());
    struct nexus_wifi_network networks[2] = {0};
    networks[0].ssid[0] = 'N'; networks[0].ssid[1] = 'X';
    networks[0].ssid_length = 2U;
    networks[0].channel = 6U;
    networks[0].signal_dbm = -48;
    networks[0].security = NEXUS_WIFI_SECURITY_WPA2_PSK;
    networks[1].ssid[0] = 'O';
    networks[1].ssid_length = 1U;
    networks[1].channel = 1U;
    networks[1].security = NEXUS_WIFI_SECURITY_OPEN;
    assert(wifi_publish_scan_results(networks, 2U));
    assert(wifi_get_state() == NEXUS_WIFI_READY);
    assert(wifi_scan_result_count() == 2U);
    struct nexus_wifi_network copied = {0};
    assert(wifi_scan_result_at(0U, &copied));
    assert(copied.ssid_length == 2U && copied.channel == 6U);
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
