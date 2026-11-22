#include "netdev.h"

#include <stddef.h>

static char registered_name[NEXUS_NETDEV_NAME_MAX + 1U];
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

    size_t name_length = 0U;
    while (name_length <= NEXUS_NETDEV_NAME_MAX &&
           name[name_length] != '\0') {
        ++name_length;
    }
    if (name_length == 0U || name_length > NEXUS_NETDEV_NAME_MAX) {
        return false;
    }

    for (size_t i = 0; i < name_length; ++i) {
        registered_name[i] = name[i];
    }
    registered_name[name_length] = '\0';
    registered_ops = ops;
    registered_context = context;
    return true;
}

bool netdev_ready(void) {
    return registered_ops != NULL;
}

const char *netdev_name(void) {
    return registered_ops != NULL ? registered_name : NULL;
}

bool netdev_get_mac(uint8_t address[6]) {
    return registered_ops != NULL && address != NULL &&
           registered_ops->get_mac(registered_context, address);
}

bool netdev_send_frame(const void *frame, size_t length) {
    if (registered_ops == NULL || frame == NULL ||
        length < NEXUS_NETDEV_MIN_FRAME_SIZE ||
        length > NEXUS_NETDEV_MAX_FRAME_SIZE) {
        return false;
    }
    return registered_ops->send_frame(registered_context, frame, length);
}

bool netdev_receive_frame(void *buffer, size_t capacity, size_t *length) {
    if (length != NULL) *length = 0U;
    if (registered_ops == NULL || buffer == NULL || length == NULL ||
        capacity < NEXUS_NETDEV_MIN_FRAME_SIZE) {
        return false;
    }

    if (!registered_ops->receive_frame(registered_context, buffer,
                                       capacity, length)) {
        *length = 0U;
        return false;
    }
    if (*length < NEXUS_NETDEV_MIN_FRAME_SIZE ||
        *length > NEXUS_NETDEV_MAX_FRAME_SIZE ||
        *length > capacity) {
        *length = 0U;
        return false;
    }
    return true;
}
