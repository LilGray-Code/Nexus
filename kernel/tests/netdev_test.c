#include "netdev.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct fake_device {
    uint8_t mac[6];
    size_t sent_length;
    size_t receive_length;
    bool receive_success;
    bool receive_overrun;
};

static bool fake_get_mac(void *context, uint8_t address[6]) {
    struct fake_device *device = context;
    memcpy(address, device->mac, sizeof(device->mac));
    return true;
}

static bool fake_send_frame(void *context, const void *frame, size_t length) {
    struct fake_device *device = context;
    assert(frame != NULL);
    device->sent_length = length;
    return true;
}

static bool fake_receive_frame(void *context, void *buffer, size_t capacity,
                               size_t *length) {
    struct fake_device *device = context;
    (void)buffer;
    (void)capacity;
    *length = device->receive_length;
    return device->receive_success || device->receive_overrun;
}

int main(void) {
    static const struct nexus_net_device_ops incomplete_ops = {
        fake_get_mac, fake_send_frame, NULL
    };
    static const struct nexus_net_device_ops ops = {
        fake_get_mac, fake_send_frame, fake_receive_frame
    };
    struct fake_device device = {
        .mac = {0x02U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U}
    };
    char mutable_name[] = "nexus0";
    uint8_t frame[NEXUS_NETDEV_MAX_FRAME_SIZE] = {0};
    uint8_t mac[6] = {0};
    size_t length = 123U;

    assert(!netdev_ready());
    assert(netdev_name() == NULL);
    assert(!netdev_register(NULL, &ops, &device));
    assert(!netdev_register("", &ops, &device));
    assert(!netdev_register("bad", &incomplete_ops, &device));
    assert(!netdev_register("0123456789abcdef", &ops, &device));
    assert(netdev_register(mutable_name, &ops, &device));
    mutable_name[0] = 'X';
    assert(netdev_ready());
    assert(strcmp(netdev_name(), "nexus0") == 0);
    assert(!netdev_register("second", &ops, &device));

    assert(!netdev_get_mac(NULL));
    assert(netdev_get_mac(mac));
    assert(memcmp(mac, device.mac, sizeof(mac)) == 0);

    assert(!netdev_send_frame(NULL, 14U));
    assert(!netdev_send_frame(frame, 13U));
    assert(!netdev_send_frame(frame, NEXUS_NETDEV_MAX_FRAME_SIZE + 1U));
    assert(netdev_send_frame(frame, 14U));
    assert(device.sent_length == 14U);
    assert(netdev_send_frame(frame, sizeof(frame)));
    assert(device.sent_length == sizeof(frame));

    assert(!netdev_receive_frame(NULL, sizeof(frame), &length));
    assert(length == 0U);
    length = 123U;
    assert(!netdev_receive_frame(frame, sizeof(frame), NULL));
    assert(!netdev_receive_frame(frame, 13U, &length));
    assert(length == 0U);

    device.receive_success = false;
    device.receive_overrun = false;
    device.receive_length = 64U;
    length = 123U;
    assert(!netdev_receive_frame(frame, sizeof(frame), &length));
    assert(length == 0U);

    device.receive_success = true;
    device.receive_length = 64U;
    assert(netdev_receive_frame(frame, sizeof(frame), &length));
    assert(length == 64U);

    device.receive_length = 13U;
    assert(!netdev_receive_frame(frame, sizeof(frame), &length));
    assert(length == 0U);

    device.receive_length = sizeof(frame) + 1U;
    device.receive_overrun = true;
    assert(!netdev_receive_frame(frame, sizeof(frame), &length));
    assert(length == 0U);

    device.receive_length = 128U;
    device.receive_overrun = false;
    assert(!netdev_receive_frame(frame, 64U, &length));
    assert(length == 0U);
    return 0;
}
