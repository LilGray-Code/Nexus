#include "netdev.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static unsigned send_calls;
static unsigned receive_calls;
static size_t receive_capacity_seen;
static uint8_t received_frame[64];

static bool fake_send(void *context, const void *frame, size_t length) {
    (void)context;
    if (frame == NULL || length < NEXUS_NETDEV_MIN_FRAME) return false;
    ++send_calls;
    return true;
}

static bool fake_receive(void *context, void *buffer, size_t capacity,
                         size_t *length) {
    (void)context;
    if (buffer == NULL || length == NULL) return false;
    ++receive_calls;
    receive_capacity_seen = capacity;
    if (capacity < sizeof(received_frame)) return false;
    for (size_t i = 0; i < sizeof(received_frame); ++i)
        ((uint8_t *)buffer)[i] = received_frame[i];
    *length = sizeof(received_frame);
    return true;
}

int main(void) {
    static const uint8_t mac[6] = {0x02U, 0x12U, 0x34U, 0x56U, 0x78U, 0x9AU};
    static const uint8_t zero_mac[6] = {0U, 0U, 0U, 0U, 0U, 0U};
    static const uint8_t multicast_mac[6] = {0x01U, 0U, 0x5EU, 0U, 0U, 1U};
    static const struct nexus_netdev_ops ops = {
        .send_frame = fake_send,
        .receive_frame = fake_receive
    };
    uint8_t frame[128] = {0};
    struct nexus_netdev_info info;

    netdev_init();
    assert(!netdev_ready());
    assert(!netdev_get_info(&info));
    assert(!netdev_register("bad0", zero_mac, 128U, &ops, NULL));
    assert(!netdev_register("bad0", multicast_mac, 128U, &ops, NULL));
    assert(!netdev_register("bad0", mac, NEXUS_NETDEV_MIN_FRAME - 1U,
                            &ops, NULL));
    assert(!netdev_register("bad0", mac, NEXUS_NETDEV_MAX_FRAME + 1U,
                            &ops, NULL));
    assert(!netdev_register("bad0", mac, 128U, NULL, NULL));

    assert(netdev_register("test0", mac, 128U, &ops, NULL));
    assert(netdev_ready());
    assert(!netdev_register("second", mac, 128U, &ops, NULL));
    assert(netdev_get_info(&info));
    assert(info.name[0] == 't' && info.name[4] == '0' &&
           info.name[5] == '\0');
    assert(info.max_frame_size == 128U);
    for (size_t i = 0; i < sizeof(mac); ++i) assert(info.mac[i] == mac[i]);

    assert(!netdev_send_frame(frame, NEXUS_NETDEV_MIN_FRAME - 1U));
    assert(!netdev_send_frame(frame, 129U));
    assert(netdev_send_frame(frame, NEXUS_NETDEV_MIN_FRAME));
    assert(send_calls == 1U);

    size_t length = 99U;
    assert(!netdev_receive_frame(frame, NEXUS_NETDEV_MIN_FRAME - 1U,
                                 &length));
    assert(length == 0U);
    assert(netdev_receive_frame(frame, sizeof(frame), &length));
    assert(receive_calls == 1U);
    assert(receive_capacity_seen == 128U);
    assert(length == sizeof(received_frame));
    return 0;
}
