#include "netdev.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct fake_adapter {
    bool link;
    bool oversized_receive;
    unsigned sends;
    unsigned receives;
    size_t capacity_seen;
};

static bool fake_send(void *context, const void *frame, size_t length) {
    struct fake_adapter *adapter = (struct fake_adapter *)context;
    if (adapter == NULL || frame == NULL ||
        length < NEXUS_NETDEV_MIN_FRAME) return false;
    ++adapter->sends;
    return true;
}

static bool fake_link_up(void *context) {
    const struct fake_adapter *adapter =
        (const struct fake_adapter *)context;
    return adapter != NULL && adapter->link;
}

static bool fake_receive(void *context, void *buffer, size_t capacity,
                         size_t *length) {
    struct fake_adapter *adapter = (struct fake_adapter *)context;
    if (adapter == NULL || buffer == NULL || length == NULL ||
        capacity < 64U) return false;
    ++adapter->receives;
    adapter->capacity_seen = capacity;
    for (size_t i = 0U; i < 64U; ++i) ((uint8_t *)buffer)[i] = (uint8_t)i;
    *length = adapter->oversized_receive ? capacity + 1U : 64U;
    return true;
}

int main(void) {
    static const uint8_t ethernet_mac[6] =
        {0x02U, 0x12U, 0x34U, 0x56U, 0x78U, 0x9AU};
    static const uint8_t wireless_mac[6] =
        {0x02U, 0x22U, 0x34U, 0x56U, 0x78U, 0x9AU};
    static const uint8_t zero_mac[6] = {0U, 0U, 0U, 0U, 0U, 0U};
    static const uint8_t multicast_mac[6] =
        {0x01U, 0U, 0x5EU, 0U, 0U, 1U};
    static const struct nexus_netdev_ops ops = {
        .send_frame = fake_send,
        .receive_frame = fake_receive,
        .link_up = fake_link_up
    };
    struct fake_adapter ethernet = {0};
    struct fake_adapter wireless = {0};
    uint8_t frame[128] = {0};
    struct nexus_netdev_info info;
    size_t length = 99U;

    netdev_init();
    assert(!netdev_ready());
    assert(!netdev_link_up());
    assert(netdev_count() == 0U);
    assert(!netdev_get_info(&info));
    assert(!netdev_get_info_at(0U, &info));
    assert(!netdev_register("bad0", (enum nexus_netdev_medium)0,
                            ethernet_mac, 128U, &ops, &ethernet));
    assert(!netdev_register("bad0", NEXUS_NETDEV_MEDIUM_ETHERNET,
                            zero_mac, 128U, &ops, &ethernet));
    assert(!netdev_register("bad0", NEXUS_NETDEV_MEDIUM_ETHERNET,
                            multicast_mac, 128U, &ops, &ethernet));
    assert(!netdev_register("bad0", NEXUS_NETDEV_MEDIUM_ETHERNET,
                            ethernet_mac, NEXUS_NETDEV_MIN_FRAME - 1U,
                            &ops, &ethernet));
    assert(!netdev_register("bad0", NEXUS_NETDEV_MEDIUM_ETHERNET,
                            ethernet_mac, 128U, NULL, &ethernet));

    assert(netdev_register("eth0", NEXUS_NETDEV_MEDIUM_ETHERNET,
                           ethernet_mac, 128U, &ops, &ethernet));
    assert(netdev_ready() && netdev_count() == 1U);
    assert(!netdev_link_up());
    assert(netdev_get_info(&info));
    assert(info.name[0] == 'e' && !info.link_up);
    assert(info.max_frame_size == 128U);
    assert(info.medium == NEXUS_NETDEV_MEDIUM_ETHERNET);
    assert(!netdev_register("eth0", NEXUS_NETDEV_MEDIUM_WIFI,
                            wireless_mac, 128U, &ops, &wireless));
    assert(!netdev_register("wlan0", NEXUS_NETDEV_MEDIUM_ETHERNET,
                            ethernet_mac, 128U, &ops, &wireless));

    wireless.link = true;
    assert(netdev_register("wlan0", NEXUS_NETDEV_MEDIUM_WIFI,
                           wireless_mac, 128U, &ops, &wireless));
    assert(netdev_count() == 2U);
    assert(netdev_link_up());
    assert(netdev_get_info(&info));
    assert(info.name[0] == 'w' && info.link_up);
    assert(info.medium == NEXUS_NETDEV_MEDIUM_WIFI);
    assert(info.mac[1] == wireless_mac[1]);
    assert(netdev_get_info_at(0U, &info));
    assert(info.name[0] == 'e' && !info.link_up);
    assert(info.medium == NEXUS_NETDEV_MEDIUM_ETHERNET);
    assert(!netdev_get_info_at(2U, &info));

    assert(netdev_send_frame(frame, NEXUS_NETDEV_MIN_FRAME));
    assert(wireless.sends == 1U && ethernet.sends == 0U);
    assert(netdev_receive_frame(frame, sizeof(frame), &length));
    assert(length == 64U && wireless.receives == 1U);
    assert(wireless.capacity_seen == 128U);

    wireless.oversized_receive = true;
    length = 99U;
    assert(!netdev_receive_frame(frame, sizeof(frame), &length));
    assert(length == 0U);

    wireless.link = false;
    ethernet.link = true;
    assert(netdev_link_up());
    assert(netdev_get_info(&info));
    assert(info.name[0] == 'e' && info.link_up);
    assert(netdev_send_frame(frame, NEXUS_NETDEV_MIN_FRAME));
    assert(ethernet.sends == 1U);
    return 0;
}
