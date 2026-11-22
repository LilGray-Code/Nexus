#ifndef NEXUS_WIFI_H
#define NEXUS_WIFI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_WIFI_MAX_SCAN_RESULTS 32U
#define NEXUS_WIFI_SSID_MAX 32U
#define NEXUS_WIFI_PASSPHRASE_MAX 63U

enum nexus_wifi_security {
    NEXUS_WIFI_SECURITY_OPEN = 0,
    NEXUS_WIFI_SECURITY_WPA2_PSK,
    NEXUS_WIFI_SECURITY_WPA3_SAE
};

enum nexus_wifi_state {
    NEXUS_WIFI_UNAVAILABLE = 0,
    NEXUS_WIFI_READY,
    NEXUS_WIFI_SCANNING,
    NEXUS_WIFI_CONNECTING,
    NEXUS_WIFI_CONNECTED,
    NEXUS_WIFI_FAILED
};

struct nexus_wifi_network {
    uint8_t ssid[NEXUS_WIFI_SSID_MAX];
    uint8_t ssid_length;
    uint8_t bssid[6];
    uint8_t channel;
    int8_t signal_dbm;
    enum nexus_wifi_security security;
};

struct nexus_wifi_driver_ops {
    bool (*start)(void *context);
    bool (*scan)(void *context);
    bool (*connect)(void *context, const struct nexus_wifi_network *network,
                    const char *passphrase);
    void (*disconnect)(void *context);
    void (*poll)(void *context);
};

/* The hardware driver owns radio operations; this layer validates requests
 * and reports only state/results explicitly supplied by that driver.
 */
bool wifi_register_driver(const struct nexus_wifi_driver_ops *ops,
                          void *context);
bool wifi_start(void);
bool wifi_scan(void);
bool wifi_publish_scan_results(const struct nexus_wifi_network *results,
                               size_t count);
size_t wifi_scan_result_count(void);
bool wifi_scan_result_at(size_t index, struct nexus_wifi_network *result);
bool wifi_connect(const struct nexus_wifi_network *network,
                  const char *passphrase);
void wifi_report_connected(void);
void wifi_report_disconnected(void);
void wifi_report_failure(void);
void wifi_disconnect(void);
void wifi_poll(void);
enum nexus_wifi_state wifi_get_state(void);
bool wifi_available(void);

#endif
