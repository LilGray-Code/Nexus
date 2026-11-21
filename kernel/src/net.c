#include "net.h"

#include "e1000.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define NET_MAX_FRAME 2048U
#define NET_ETHERNET_HEADER 14U
#define NET_IPV4_HEADER 20U
#define NET_UDP_HEADER 8U
#define NET_ARP_PACKET 28U
#define NET_ARP_CACHE_SIZE 8U
#define ETHERTYPE_IPV4 0x0800U
#define ETHERTYPE_ARP 0x0806U
#define IP_PROTOCOL_UDP 17U

struct net_arp_entry {
    bool valid;
    uint8_t ipv4[4];
    uint8_t mac[6];
};

struct net_state {
    bool ready;
    uint8_t mac[6];
    uint8_t ipv4[4];
    uint16_t ipv4_id;
    struct net_arp_entry arp[NET_ARP_CACHE_SIZE];
    size_t arp_replace;
};

static struct net_state network;
static uint8_t rx_frame[NET_MAX_FRAME];

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void write_be16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void write_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0; i < length; ++i) out[i] = in[i];
}

static bool equal_bytes(const uint8_t *a, const uint8_t *b, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static bool ipv4_zero(const uint8_t address[4]) {
    return address[0] == 0U && address[1] == 0U &&
           address[2] == 0U && address[3] == 0U;
}

static uint16_t checksum(const uint8_t *data, size_t length) {
    uint32_t sum = 0U;
    size_t i = 0U;
    while (i + 1U < length) {
        sum += ((uint32_t)data[i] << 8) | data[i + 1U];
        i += 2U;
    }
    if (i < length) sum += (uint32_t)data[i] << 8;
    while ((sum >> 16) != 0U) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}

static bool send_ethernet(const uint8_t destination[6], uint16_t ethertype,
                          const void *payload, size_t length) {
    if (!network.ready || destination == NULL ||
        (length != 0U && payload == NULL) ||
        length > NET_MAX_FRAME - NET_ETHERNET_HEADER) {
        return false;
    }
    uint8_t frame[NET_MAX_FRAME];
    copy_bytes(frame, destination, 6U);
    copy_bytes(frame + 6U, network.mac, 6U);
    write_be16(frame + 12U, ethertype);
    if (length != 0U) copy_bytes(frame + NET_ETHERNET_HEADER, payload, length);
    return e1000_send_frame(frame, NET_ETHERNET_HEADER + length);
}

static void arp_update(const uint8_t address[4], const uint8_t mac[6]) {
    if (ipv4_zero(address)) return;
    size_t slot = NET_ARP_CACHE_SIZE;
    for (size_t i = 0; i < NET_ARP_CACHE_SIZE; ++i) {
        if (network.arp[i].valid &&
            equal_bytes(network.arp[i].ipv4, address, 4U)) {
            slot = i;
            break;
        }
    }
    if (slot == NET_ARP_CACHE_SIZE) {
        slot = network.arp_replace;
        network.arp_replace = (network.arp_replace + 1U) % NET_ARP_CACHE_SIZE;
    }
    network.arp[slot].valid = true;
    copy_bytes(network.arp[slot].ipv4, address, 4U);
    copy_bytes(network.arp[slot].mac, mac, 6U);
}

static bool arp_lookup(const uint8_t address[4], uint8_t mac[6]) {
    for (size_t i = 0; i < NET_ARP_CACHE_SIZE; ++i) {
        if (network.arp[i].valid &&
            equal_bytes(network.arp[i].ipv4, address, 4U)) {
            copy_bytes(mac, network.arp[i].mac, 6U);
            return true;
        }
    }
    return false;
}

static bool send_arp_request(const uint8_t target[4]) {
    uint8_t arp[NET_ARP_PACKET] = {0};
    write_be16(arp, 1U);
    write_be16(arp + 2U, ETHERTYPE_IPV4);
    arp[4] = 6U;
    arp[5] = 4U;
    write_be16(arp + 6U, 1U);
    copy_bytes(arp + 8U, network.mac, 6U);
    copy_bytes(arp + 14U, network.ipv4, 4U);
    copy_bytes(arp + 24U, target, 4U);
    static const uint8_t broadcast[6] =
        {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
    return send_ethernet(broadcast, ETHERTYPE_ARP, arp, sizeof(arp));
}

static bool send_arp_reply(const uint8_t target_mac[6],
                           const uint8_t target_ip[4]) {
    uint8_t arp[NET_ARP_PACKET] = {0};
    write_be16(arp, 1U);
    write_be16(arp + 2U, ETHERTYPE_IPV4);
    arp[4] = 6U;
    arp[5] = 4U;
    write_be16(arp + 6U, 2U);
    copy_bytes(arp + 8U, network.mac, 6U);
    copy_bytes(arp + 14U, network.ipv4, 4U);
    copy_bytes(arp + 18U, target_mac, 6U);
    copy_bytes(arp + 24U, target_ip, 4U);
    return send_ethernet(target_mac, ETHERTYPE_ARP, arp, sizeof(arp));
}

static void receive_arp(const uint8_t *arp, size_t length) {
    if (length < NET_ARP_PACKET || read_be16(arp) != 1U ||
        read_be16(arp + 2U) != ETHERTYPE_IPV4 ||
        arp[4] != 6U || arp[5] != 4U) {
        return;
    }
    const uint16_t operation = read_be16(arp + 6U);
    const uint8_t *sender_mac = arp + 8U;
    const uint8_t *sender_ip = arp + 14U;
    const uint8_t *target_mac = arp + 18U;
    const uint8_t *target_ip = arp + 24U;
    arp_update(sender_ip, sender_mac);
    (void)target_mac;

    if (operation == 1U && !ipv4_zero(network.ipv4) &&
        equal_bytes(target_ip, network.ipv4, 4U)) {
        (void)send_arp_reply(sender_mac, sender_ip);
    }
}

static void receive_ipv4(const uint8_t *ip, size_t length) {
    if (length < NET_IPV4_HEADER || (ip[0] >> 4) != 4U) return;
    const size_t header_length = (size_t)(ip[0] & 0x0FU) * 4U;
    const uint16_t total_length = read_be16(ip + 2U);
    if (header_length < NET_IPV4_HEADER || header_length > length ||
        total_length < header_length || total_length > length ||
        checksum(ip, header_length) != 0U) {
        return;
    }

    if (ip[9] != IP_PROTOCOL_UDP) return;
    /* UDP demultiplexing is added with DHCP/DNS in the next network milestone. */
}

static void receive_frame(const uint8_t *frame, size_t length) {
    if (frame == NULL || length < NET_ETHERNET_HEADER) return;
    const uint16_t type = read_be16(frame + 12U);
    const uint8_t *payload = frame + NET_ETHERNET_HEADER;
    const size_t payload_length = length - NET_ETHERNET_HEADER;
    if (type == ETHERTYPE_ARP) {
        receive_arp(payload, payload_length);
    } else if (type == ETHERTYPE_IPV4) {
        receive_ipv4(payload, payload_length);
    }
}

bool net_init(void) {
    network = (struct net_state){0};
    if (!e1000_get_mac(network.mac)) return false;
    network.ready = true;
    network.ipv4_id = 1U;
    serial_write("NEXUS ETHERNET L2 READY\r\n");
    return true;
}

void net_poll(void) {
    if (!network.ready) return;
    for (size_t i = 0; i < 32U; ++i) {
        size_t length = 0U;
        if (!e1000_receive_frame(rx_frame, sizeof(rx_frame), &length)) break;
        receive_frame(rx_frame, length);
    }
}

bool net_get_info(struct nexus_net_info *info) {
    if (info == NULL || !network.ready) return false;
    info->interface_ready = true;
    info->ipv4_configured = !ipv4_zero(network.ipv4);
    copy_bytes(info->mac, network.mac, sizeof(info->mac));
    copy_bytes(info->ipv4, network.ipv4, sizeof(info->ipv4));
    return true;
}

bool net_udp_send(const uint8_t destination_ipv4[4],
                  uint16_t source_port, uint16_t destination_port,
                  const void *payload, uint16_t payload_length) {
    if (!network.ready || destination_ipv4 == NULL ||
        (payload_length != 0U && payload == NULL) || payload_length > 1400U) {
        return false;
    }
    uint8_t destination_mac[6];
    if (!arp_lookup(destination_ipv4, destination_mac)) {
        (void)send_arp_request(destination_ipv4);
        return false;
    }

    uint8_t packet[NET_IPV4_HEADER + NET_UDP_HEADER + 1400U];
    const size_t udp_length = NET_UDP_HEADER + payload_length;
    const size_t ip_length = NET_IPV4_HEADER + udp_length;
    packet[0] = 0x45U;
    packet[1] = 0U;
    write_be16(packet + 2U, (uint16_t)ip_length);
    write_be16(packet + 4U, network.ipv4_id++);
    write_be16(packet + 6U, 0U);
    packet[8] = 64U;
    packet[9] = IP_PROTOCOL_UDP;
    write_be16(packet + 10U, 0U);
    copy_bytes(packet + 12U, network.ipv4, 4U);
    copy_bytes(packet + 16U, destination_ipv4, 4U);
    write_be16(packet + 10U, checksum(packet, NET_IPV4_HEADER));

    uint8_t *udp = packet + NET_IPV4_HEADER;
    write_be16(udp, source_port);
    write_be16(udp + 2U, destination_port);
    write_be16(udp + 4U, (uint16_t)udp_length);
    write_be16(udp + 6U, 0U);
    if (payload_length != 0U) {
        copy_bytes(udp + NET_UDP_HEADER, payload, payload_length);
    }
    return send_ethernet(destination_mac, ETHERTYPE_IPV4, packet, ip_length);
}
