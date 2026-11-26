#ifndef NEXUS_NETDEV_H
#define NEXUS_NETDEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_NETDEV_NAME_MAX 16U
#define NEXUS_NETDEV_MAX_DEVICES 4U
#define NEXUS_NETDEV_MIN_FRAME 14U
#define NEXUS_NETDEV_MAX_FRAME 2048U

/* Medium is descriptive metadata; Wi-Fi still needs a real 802.11 station driver. */
enum nexus_netdev_medium {
    NEXUS_NETDEV_MEDIUM_ETHERNET = 1,
    NEXUS_NETDEV_MEDIUM_WIFI = 2
};

/*
 * Drivers exchange Ethernet-II frames with the IPv4 stack. An IEEE 802.11
 * station driver must own scanning, association, authentication, encryption,
 * and 802.11/Ethernet encapsulation before registering here.
 */
struct nexus_netdev_ops {
    bool (*send_frame)(void *context, const void *frame, size_t length);
    bool (*receive_frame)(void *context, void *buffer, size_t capacity,
                          size_t *length);
    /* Optional live physical-link query; false means link is not usable. */
    bool (*link_up)(void *context);
    /* Optional negotiated speed in Mbit/s; zero means unknown/unavailable. */
    uint32_t (*link_speed_mbps)(void *context);
};

struct nexus_netdev_info {
    char name[NEXUS_NETDEV_NAME_MAX];
    uint8_t mac[6];
    size_t max_frame_size;
    enum nexus_netdev_medium medium;
    bool link_up;
    bool active;
    bool preferred;
    uint32_t link_speed_mbps;
    uint64_t tx_frames;
    uint64_t rx_frames;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint32_t tx_errors;
    uint32_t rx_errors;
};

void netdev_init(void);
bool netdev_register(const char *name, enum nexus_netdev_medium medium,
                     const uint8_t mac[6], size_t max_frame_size,
                     const struct nexus_netdev_ops *ops, void *context);
size_t netdev_count(void);
bool netdev_get_info_at(size_t index, struct nexus_netdev_info *info);
bool netdev_ready(void);
/* The preferred device is automatically restored when its link returns. */
bool netdev_select_device(size_t index);
void netdev_use_automatic_device(void);
size_t netdev_active_index(void);
/* Keeps a live current interface; falls back to another linked adapter. */
bool netdev_link_up(void);
bool netdev_get_info(struct nexus_netdev_info *info);
bool netdev_send_frame(const void *frame, size_t length);
bool netdev_receive_frame(void *buffer, size_t capacity, size_t *length);

#endif
