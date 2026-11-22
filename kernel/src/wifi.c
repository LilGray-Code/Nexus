#include "wifi.h"

#include <stddef.h>
#include <stdint.h>

static const struct nexus_wifi_driver_ops *driver_ops;
static void *driver_context;
static enum nexus_wifi_state state = NEXUS_WIFI_UNAVAILABLE;
static struct nexus_wifi_network scan_results[NEXUS_WIFI_MAX_SCAN_RESULTS];
static size_t scan_count;

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
    driver_ops = ops;
    driver_context = context;
    state = NEXUS_WIFI_READY;
    scan_count = 0U;
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
        if (passphrase_length < 8U ||
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
