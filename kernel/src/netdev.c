#include "netdev.h"

struct netdev_state {
    bool registered;
    struct nexus_netdev_info info;
    struct nexus_netdev_ops ops;
    void *context;
};

static struct netdev_state active_device;

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0; i < length; ++i) out[i] = in[i];
}

static bool valid_mac(const uint8_t mac[6]) {
    uint8_t any = 0U;
    for (size_t i = 0; i < 6U; ++i) any |= mac[i];
    return any != 0U && (mac[0] & 1U) == 0U;
}

void netdev_init(void) {
    active_device = (struct netdev_state){0};
}

bool netdev_register(const char *name, const uint8_t mac[6],
                     size_t max_frame_size,
                     const struct nexus_netdev_ops *ops, void *context) {
    if (active_device.registered || name == NULL || name[0] == '\0' ||
        mac == NULL || !valid_mac(mac) || ops == NULL ||
        ops->send_frame == NULL || ops->receive_frame == NULL ||
        max_frame_size < NEXUS_NETDEV_MIN_FRAME ||
        max_frame_size > NEXUS_NETDEV_MAX_FRAME) return false;

    size_t name_length = 0U;
    while (name_length < NEXUS_NETDEV_NAME_MAX &&
           name[name_length] != '\0') ++name_length;
    if (name_length == 0U || name_length == NEXUS_NETDEV_NAME_MAX) return false;

    active_device.info = (struct nexus_netdev_info){0};
    copy_bytes(active_device.info.name, name, name_length);
    copy_bytes(active_device.info.mac, mac, sizeof(active_device.info.mac));
    active_device.info.max_frame_size = max_frame_size;
    active_device.ops = *ops;
    active_device.context = context;
    active_device.registered = true;
    return true;
}

bool netdev_ready(void) {
    return active_device.registered;
}

bool netdev_link_up(void) {
    return active_device.registered && active_device.ops.link_up != NULL &&
           active_device.ops.link_up(active_device.context);
}

bool netdev_get_info(struct nexus_netdev_info *info) {
    if (!active_device.registered || info == NULL) return false;
    *info = active_device.info;
    return true;
}

bool netdev_send_frame(const void *frame, size_t length) {
    if (!active_device.registered || frame == NULL ||
        length < NEXUS_NETDEV_MIN_FRAME ||
        length > active_device.info.max_frame_size) return false;
    return active_device.ops.send_frame(active_device.context, frame, length);
}

bool netdev_receive_frame(void *buffer, size_t capacity, size_t *length) {
    if (length != NULL) *length = 0U;
    if (!active_device.registered || buffer == NULL || length == NULL ||
        capacity < NEXUS_NETDEV_MIN_FRAME) return false;
    if (capacity > active_device.info.max_frame_size)
        capacity = active_device.info.max_frame_size;
    if (!active_device.ops.receive_frame(active_device.context, buffer,
                                          capacity, length)) {
        *length = 0U;
        return false;
    }
    /* A driver must never report bytes outside the caller's buffer. */
    if (*length < NEXUS_NETDEV_MIN_FRAME || *length > capacity) {
        *length = 0U;
        return false;
    }
    return true;
}
