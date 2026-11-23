#include "wifi.h"
#include "netdev.h"

#include <stddef.h>
#include <stdint.h>

static const struct nexus_wifi_driver_ops *driver_ops;
static void *driver_context;
static enum nexus_wifi_state state = NEXUS_WIFI_UNAVAILABLE;
static struct nexus_wifi_network scan_results[NEXUS_WIFI_MAX_SCAN_RESULTS];
static size_t scan_count;
static bool network_adapter_registered;


static bool wifi_netdev_get_mac(void *context, uint8_t address[6]) {
    return context == driver_context && driver_ops != NULL &&
           driver_ops->get_mac != NULL &&
           driver_ops->get_mac(driver_context, address);
}

static bool wifi_netdev_send_frame(void *context, const void *frame,
                                   size_t length) {
    return context == driver_context && driver_ops != NULL &&
           driver_ops->send_frame != NULL &&
           driver_ops->send_frame(driver_context, frame, length);
}

static bool wifi_netdev_receive_frame(void *context, void *buffer,
                                      size_t capacity, size_t *length) {
    if (length != NULL) *length = 0U;
    return context == driver_context && driver_ops != NULL &&
           driver_ops->receive_frame != NULL &&
           driver_ops->receive_frame(driver_context, buffer, capacity, length);
}

static const struct nexus_net_device_ops wifi_netdev_ops = {
    .get_mac = wifi_netdev_get_mac,
    .send_frame = wifi_netdev_send_frame,
    .receive_frame = wifi_netdev_receive_frame
};

static bool valid_network(const struct nexus_wifi_network *network) {
    if (network == NULL || network->ssid_length == 0U ||
        network->ssid_length > NEXUS_WIFI_SSID_MAX ||
        network->channel == 0U ||
        network->security > NEXUS_WIFI_SECURITY_WPA3_SAE) return false;
    return true;
}

bool wifi_register_driver(const struct nexus_wifi_driver_ops *ops,
                          void *context) {
    if (driver_ops != NULL || ops == NULL || ops->start == NULL ||
        ops->scan == NULL || ops->connect == NULL ||
        ops->disconnect == NULL || ops->poll == NULL) return false;

    const bool has_any_frame_callback =
        ops->get_mac != NULL || ops->send_frame != NULL ||
        ops->receive_frame != NULL;
    const bool has_all_frame_callbacks =
        ops->get_mac != NULL && ops->send_frame != NULL &&
        ops->receive_frame != NULL;
    if (has_any_frame_callback && !has_all_frame_callbacks) return false;

    driver_ops = ops;
    driver_context = context;
    state = NEXUS_WIFI_READY;
    scan_count = 0U;
    network_adapter_registered = false;

    if (has_all_frame_callbacks) {
        if (!netdev_register("wlan0", &wifi_netdev_ops, context)) {
            driver_ops = NULL;
            driver_context = NULL;
            state = NEXUS_WIFI_UNAVAILABLE;
            return false;
        }
        network_adapter_registered = true;
    }
    return true;
}

bool wifi_start(void) {
    if (driver_ops == NULL) return false;
    if (!driver_ops->start(driver_context)) {
        state = NEXUS_WIFI_FAILED;
        return false;
    }
    state = NEXUS_WIFI_READY;
    return true;
}

bool wifi_scan(void) {
    if (driver_ops == NULL || state == NEXUS_WIFI_UNAVAILABLE ||
        state == NEXUS_WIFI_SCANNING || state == NEXUS_WIFI_CONNECTING) {
        return false;
    }
    scan_count = 0U;
    if (!driver_ops->scan(driver_context)) {
        state = NEXUS_WIFI_FAILED;
        return false;
    }
    state = NEXUS_WIFI_SCANNING;
    return true;
}

bool wifi_publish_scan_results(const struct nexus_wifi_network *results,
                               size_t count) {
    if (driver_ops == NULL || state != NEXUS_WIFI_SCANNING ||
        count > NEXUS_WIFI_MAX_SCAN_RESULTS ||
        (count != 0U && results == NULL)) return false;
    for (size_t i = 0U; i < count; ++i) {
        if (!valid_network(&results[i])) return false;
    }
    for (size_t i = 0U; i < count; ++i) scan_results[i] = results[i];
    scan_count = count;
    state = NEXUS_WIFI_READY;
    return true;
}

size_t wifi_scan_result_count(void) { return scan_count; }

bool wifi_scan_result_at(size_t index, struct nexus_wifi_network *result) {
    if (result == NULL || index >= scan_count) return false;
    *result = scan_results[index];
    return true;
}

bool wifi_connect(const struct nexus_wifi_network *network,
                  const char *passphrase) {
    if (driver_ops == NULL || !valid_network(network) ||
        (state != NEXUS_WIFI_READY && state != NEXUS_WIFI_CONNECTED) ||
        (network->security == NEXUS_WIFI_SECURITY_OPEN &&
         passphrase != NULL && passphrase[0] != '\0')) return false;

    size_t passphrase_length = 0U;
    if (network->security != NEXUS_WIFI_SECURITY_OPEN) {
        if (passphrase == NULL) return false;
        while (passphrase_length <= NEXUS_WIFI_PASSPHRASE_MAX &&
               passphrase[passphrase_length] != '\0') ++passphrase_length;
        const size_t minimum_length =
            network->security == NEXUS_WIFI_SECURITY_WPA2_PSK ? 8U : 1U;
        if (passphrase_length < minimum_length ||
            passphrase_length > NEXUS_WIFI_PASSPHRASE_MAX) return false;
    }
    if (!driver_ops->connect(driver_context, network, passphrase)) {
        state = NEXUS_WIFI_FAILED;
        return false;
    }
    state = NEXUS_WIFI_CONNECTING;
    return true;
}

void wifi_report_connected(void) {
    if (driver_ops != NULL && state == NEXUS_WIFI_CONNECTING)
        state = NEXUS_WIFI_CONNECTED;
}

void wifi_report_disconnected(void) {
    if (driver_ops != NULL && state != NEXUS_WIFI_UNAVAILABLE)
        state = NEXUS_WIFI_READY;
}

void wifi_report_failure(void) {
    if (driver_ops != NULL) state = NEXUS_WIFI_FAILED;
}

void wifi_disconnect(void) {
    if (driver_ops == NULL) return;
    driver_ops->disconnect(driver_context);
    state = NEXUS_WIFI_READY;
}

void wifi_poll(void) {
    if (driver_ops != NULL) driver_ops->poll(driver_context);
}

enum nexus_wifi_state wifi_get_state(void) { return state; }
bool wifi_available(void) { return driver_ops != NULL; }
bool wifi_network_adapter_registered(void) {
    return network_adapter_registered;
}
