#ifndef NEXUS_NETDEV_H
#define NEXUS_NETDEV_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define NEXUS_NETDEV_MIN_FRAME_SIZE 14U
#define NEXUS_NETDEV_MAX_FRAME_SIZE 2048U
#define NEXUS_NETDEV_NAME_MAX 15U
#define NEXUS_NETDEV_MAX_DEVICES 8U
/* Wi-Fi drivers bridge 802.11 frames to this Ethernet-frame interface.
 * Names are copied at registration; ops remain valid for system lifetime.
 */
struct nexus_net_device_ops {
 bool (*get_mac)(void *, uint8_t[6]);
 bool (*send_frame)(void *, const void *, size_t);
 bool (*receive_frame)(void *, void *, size_t, size_t *);
};
bool netdev_register(const char *, const struct nexus_net_device_ops *, void *);
uint32_t netdev_device_count(void);
const char *netdev_device_name_at(uint32_t);
bool netdev_set_default(uint32_t);
uint32_t netdev_default_index(void);
bool netdev_get_mac_at(uint32_t, uint8_t[6]);
bool netdev_send_frame_at(uint32_t, const void *, size_t);
bool netdev_receive_frame_at(uint32_t, void *, size_t, size_t *);
bool netdev_ready(void);
const char *netdev_name(void);
bool netdev_get_mac(uint8_t[6]);
bool netdev_send_frame(const void *, size_t);
bool netdev_receive_frame(void *, size_t, size_t *);
#endif
