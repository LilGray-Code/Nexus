#ifndef NEXUS_NETDEV_H
#define NEXUS_NETDEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_NETDEV_NAME_MAX 16U
#define NEXUS_NETDEV_MIN_FRAME 14U
#define NEXUS_NETDEV_MAX_FRAME 2048U

struct nexus_netdev_ops {
    bool (*send_frame)(void *context, const void *frame, size_t length);
    bool (*receive_frame)(void *context, void *buffer, size_t capacity,
                          size_t *length);
};

struct nexus_netdev_info {
    char name[NEXUS_NETDEV_NAME_MAX];
    uint8_t mac[6];
    size_t max_frame_size;
};

void netdev_init(void);
bool netdev_register(const char *name, const uint8_t mac[6],
                     size_t max_frame_size,
                     const struct nexus_netdev_ops *ops, void *context);
bool netdev_ready(void);
bool netdev_get_info(struct nexus_netdev_info *info);
bool netdev_send_frame(const void *frame, size_t length);
bool netdev_receive_frame(void *buffer, size_t capacity, size_t *length);

#endif
