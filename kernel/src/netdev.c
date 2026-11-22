#include "netdev.h"

#include <stddef.h>

static const char *registered_name;
static const struct nexus_net_device_ops *registered_ops;
static void *registered_context;

bool netdev_register(const char *name,
                     const struct nexus_net_device_ops *ops,
                     void *context) {
    if (name == NULL || name[0] == '\0' || ops == NULL ||
        ops->get_mac == NULL || ops->send_frame == NULL ||
        ops->receive_frame == NULL || registered_ops != NULL) {
        return false;
    }

    registered_name = name;
    registered_ops = ops;
    registered_context = context;
    return true;
}

bool netdev_ready(void) {
    return registered_ops != NULL;
}

const char *netdev_name(void) {
    return registered_name;
}

bool netdev_get_mac(uint8_t address[6]) {
    return registered_ops != NULL && address != NULL &&
           registered_ops->get_mac(registered_context, address);
}

bool netdev_send_frame(const void *frame, size_t length) {
    return registered_ops != NULL &&
           registered_ops->send_frame(registered_context, frame, length);
}

bool netdev_receive_frame(void *buffer, size_t capacity, size_t *length) {
    if (length != NULL) *length = 0U;
    return registered_ops != NULL &&
           registered_ops->receive_frame(registered_context, buffer,
                                         capacity, length);
}
