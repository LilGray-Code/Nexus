#ifndef NEXUS_NETDEV_H
#define NEXUS_NETDEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_NETDEV_MIN_FRAME_SIZE 14U
#define NEXUS_NETDEV_MAX_FRAME_SIZE 2048U
#define NEXUS_NETDEV_NAME_MAX 15U

/* Link-layer Ethernet-frame interface shared by wired and Wi-Fi drivers.
 * A wireless driver must present Ethernet frames after its 802.11 link layer.
 * Names are copied at registration; ops must remain valid for system lifetime.
 */
struct nexus_net_device_ops {
    bool (*get_mac)(void *context, uint8_t address[6]);
    bool (*send_frame)(void *context, const void *frame, size_t length);
    bool (*receive_frame)(void *context, void *buffer, size_t capacity,
                          size_t *length);
};

bool netdev_register(const char *name,
                     const struct nexus_net_device_ops *ops,
                     void *context);
bool netdev_ready(void);
const char *netdev_name(void);
bool netdev_get_mac(uint8_t address[6]);
bool netdev_send_frame(const void *frame, size_t length);
bool netdev_receive_frame(void *buffer, size_t capacity, size_t *length);

#endif
