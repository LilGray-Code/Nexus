#include "netdev.h"

struct netdev_state {
    struct nexus_netdev_info info;
    struct nexus_netdev_ops ops;
    void *context;
};

static struct netdev_state devices[NEXUS_NETDEV_MAX_DEVICES];
static size_t device_count;
static size_t active_device_index;
static size_t preferred_device_index = NEXUS_NETDEV_MAX_DEVICES;

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0; i < length; ++i) out[i] = in[i];
}

static bool equal_bytes(const uint8_t *a, const uint8_t *b, size_t length) {
    for (size_t i = 0U; i < length; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static bool equal_name(const char *a, const char *b) {
    size_t i = 0U;
    while (i < NEXUS_NETDEV_NAME_MAX) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
        ++i;
    }
    return false;
}

static bool valid_mac(const uint8_t mac[6]) {
    uint8_t any = 0U;
    for (size_t i = 0; i < 6U; ++i) any |= mac[i];
    return any != 0U && (mac[0] & 1U) == 0U;
}

static bool device_link_up(const struct netdev_state *device) {
    return device != NULL && device->ops.link_up != NULL &&
           device->ops.link_up(device->context);
}

static size_t select_active_device(void) {
    if (device_count == 0U) return NEXUS_NETDEV_MAX_DEVICES;
    if (active_device_index >= device_count) active_device_index = 0U;

    /* Prefer the user's selected adapter whenever its physical link returns. */
    if (preferred_device_index < device_count &&
        device_link_up(&devices[preferred_device_index])) {
        active_device_index = preferred_device_index;
        return active_device_index;
    }

    if (device_link_up(&devices[active_device_index]))
        return active_device_index;

    for (size_t i = 0U; i < device_count; ++i) {
        if (device_link_up(&devices[i])) {
            active_device_index = i;
            return active_device_index;
        }
    }

    /* Never send through a stale/down interface when no adapter is linked. */
    return NEXUS_NETDEV_MAX_DEVICES;
}

void netdev_init(void) {
    for (size_t i = 0U; i < NEXUS_NETDEV_MAX_DEVICES; ++i)
        devices[i] = (struct netdev_state){0};
    device_count = 0U;
    active_device_index = 0U;
    preferred_device_index = NEXUS_NETDEV_MAX_DEVICES;
}

bool netdev_register(const char *name, enum nexus_netdev_medium medium,
                     const uint8_t mac[6], size_t max_frame_size,
                     const struct nexus_netdev_ops *ops, void *context) {
    if (device_count >= NEXUS_NETDEV_MAX_DEVICES ||
        name == NULL || name[0] == '\0' ||
        (medium != NEXUS_NETDEV_MEDIUM_ETHERNET &&
         medium != NEXUS_NETDEV_MEDIUM_WIFI) ||
        mac == NULL || !valid_mac(mac) || ops == NULL ||
        ops->send_frame == NULL || ops->receive_frame == NULL ||
        max_frame_size < NEXUS_NETDEV_MIN_FRAME ||
        max_frame_size > NEXUS_NETDEV_MAX_FRAME) return false;

    size_t name_length = 0U;
    while (name_length < NEXUS_NETDEV_NAME_MAX &&
           name[name_length] != '\0') ++name_length;
    if (name_length == 0U || name_length == NEXUS_NETDEV_NAME_MAX) return false;

    for (size_t i = 0U; i < device_count; ++i) {
        if (equal_name(devices[i].info.name, name) ||
            equal_bytes(devices[i].info.mac, mac, sizeof(devices[i].info.mac)))
            return false;
    }

    struct netdev_state *device = &devices[device_count];
    *device = (struct netdev_state){0};
    copy_bytes(device->info.name, name, name_length);
    copy_bytes(device->info.mac, mac, sizeof(device->info.mac));
    device->info.max_frame_size = max_frame_size;
    device->info.medium = medium;
    device->ops = *ops;
    device->context = context;
    if (device_count == 0U) active_device_index = 0U;
    ++device_count;
    return true;
}

size_t netdev_count(void) {
    return device_count;
}

bool netdev_select_device(size_t index) {
    if (index >= device_count || !device_link_up(&devices[index]))
        return false;
    preferred_device_index = index;
    active_device_index = index;
    return true;
}

void netdev_use_automatic_device(void) {
    preferred_device_index = NEXUS_NETDEV_MAX_DEVICES;
}

size_t netdev_active_index(void) {
    return select_active_device();
}

bool netdev_get_info_at(size_t index, struct nexus_netdev_info *info) {
    if (index >= device_count || info == NULL) return false;
    const size_t active = select_active_device();
    *info = devices[index].info;
    info->link_up = device_link_up(&devices[index]);
    info->active = index == active;
    info->preferred = index == preferred_device_index;
    info->link_speed_mbps =
        devices[index].ops.link_speed_mbps != NULL && info->link_up
            ? devices[index].ops.link_speed_mbps(devices[index].context)
            : 0U;
    return true;
}

bool netdev_ready(void) {
    return device_count != 0U;
}

bool netdev_link_up(void) {
    const size_t active = select_active_device();
    return active < device_count && device_link_up(&devices[active]);
}

bool netdev_get_info(struct nexus_netdev_info *info) {
    if (info == NULL) return false;
    const size_t active = select_active_device();
    if (active >= device_count) return false;
    *info = devices[active].info;
    info->link_up = true;
    info->active = true;
    info->preferred = active == preferred_device_index;
    info->link_speed_mbps =
        devices[active].ops.link_speed_mbps != NULL
            ? devices[active].ops.link_speed_mbps(devices[active].context)
            : 0U;
    return true;
}

bool netdev_send_frame(const void *frame, size_t length) {
    const size_t active = select_active_device();
    if (active >= device_count) return false;
    struct netdev_state *device = &devices[active];
    if (frame == NULL || length < NEXUS_NETDEV_MIN_FRAME ||
        length > device->info.max_frame_size) {
        ++device->info.tx_errors;
        return false;
    }
    if (!device->ops.send_frame(device->context, frame, length)) {
        ++device->info.tx_errors;
        return false;
    }
    ++device->info.tx_frames;
    device->info.tx_bytes += length;
    return true;
}

bool netdev_receive_frame(void *buffer, size_t capacity, size_t *length) {
    if (length != NULL) *length = 0U;
    const size_t active = select_active_device();
    if (active >= device_count || buffer == NULL || length == NULL ||
        capacity < NEXUS_NETDEV_MIN_FRAME) return false;
    struct netdev_state *device = &devices[active];
    if (capacity > device->info.max_frame_size)
        capacity = device->info.max_frame_size;
    if (!device->ops.receive_frame(device->context, buffer, capacity, length)) {
        *length = 0U;
        return false; /* No frame available is not a receive error. */
    }
    /* A driver must never report bytes outside the caller's buffer. */
    if (*length < NEXUS_NETDEV_MIN_FRAME || *length > capacity) {
        *length = 0U;
        ++device->info.rx_errors;
        return false;
    }
    ++device->info.rx_frames;
    device->info.rx_bytes += *length;
    return true;
}
