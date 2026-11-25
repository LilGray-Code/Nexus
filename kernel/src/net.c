#include "net.h"

#include "netdev.h"
#include "serial.h"
#include "timer.h"

#include <stddef.h>
#include <stdint.h>

#define NET_MAX_FRAME 2048U
#define NET_ETHERNET_HEADER 14U
#define NET_IPV4_HEADER 20U
#define NET_UDP_HEADER 8U
#define NET_ARP_PACKET 28U
#define NET_ARP_CACHE_SIZE 8U
#define NET_ARP_RETRY_TICKS 25U
#define NET_DHCP_OPTIONS_OFFSET 240U
#define NET_DHCP_PACKET_SIZE 320U
#define NET_DHCP_RETRY_TICKS 300U
#define NET_DHCP_MAX_RETRIES 5U
#define NET_DHCP_CLIENT_PORT 68U
#define NET_DHCP_SERVER_PORT 67U
#define NET_DNS_SERVER_PORT 53U
#define NET_DNS_CLIENT_PORT 53000U
#define NET_DNS_PACKET_SIZE 512U
#define NET_DNS_CACHE_SIZE 8U
#define NET_DNS_HOSTNAME_MAX 253U
#define NET_DNS_MAX_TTL_SECONDS 300U
#define NET_DNS_TICKS_PER_SECOND 100U
#define NET_DNS_MAX_CNAME_HOPS 8U
#define NET_DNS_RETRY_INITIAL_TICKS 100U
#define NET_DNS_MAX_RETRIES 2U
#define ETHERTYPE_IPV4 0x0800U
#define ETHERTYPE_ARP 0x0806U
#define IP_PROTOCOL_UDP 17U
#define IP_PROTOCOL_TCP 6U
#define NET_TCP_HEADER 20U
#define NET_TCP_MAX_PAYLOAD 1200U
#define NET_TCP_RX_CAPACITY 4096U
#define NET_TCP_REORDER_SLOTS 4U
#define NET_TCP_REORDER_PAYLOAD_MAX 1920U
#define NET_TCP_RETRY_TICKS 25U
#define NET_TCP_MAX_RETRIES 5U
#define NET_TCP_DATA_TIMEOUT_TICKS (NET_TCP_RETRY_TICKS * (NET_TCP_MAX_RETRIES + 1U))
#define TCP_FLAG_FIN 0x01U
#define TCP_FLAG_SYN 0x02U
#define TCP_FLAG_RST 0x04U
#define TCP_FLAG_PSH 0x08U
#define TCP_FLAG_ACK 0x10U

struct net_arp_entry {
    bool valid;
    uint8_t ipv4[4];
    uint8_t mac[6];
};

struct net_state {
    bool ready;
    bool link_up;
    char interface_name[16];
    enum nexus_netdev_medium interface_medium;
    uint8_t mac[6];
    uint8_t ipv4[4];
    uint16_t ipv4_id;
    struct net_arp_entry arp[NET_ARP_CACHE_SIZE];
    size_t arp_replace;
};

enum net_dhcp_state {
    NET_DHCP_IDLE = 0,
    NET_DHCP_DISCOVERING,
    NET_DHCP_REQUESTING,
    NET_DHCP_BOUND
};

static struct net_state network;
static enum net_dhcp_state dhcp_state;
static uint8_t offered_ipv4[4];
static uint8_t dhcp_server_ipv4[4];
static uint8_t subnet_mask[4];
static uint8_t gateway_ipv4[4];
static uint8_t dns_ipv4[4];
static uint32_t dhcp_transaction_id;
static uint64_t dhcp_last_send_tick;
static uint32_t dhcp_retry_count;
static bool dns_query_active;
static bool dns_query_done;
static bool dns_query_success;
static uint16_t dns_query_id;
static uint16_t dns_next_id = 0x4E58U;
static uint8_t dns_query_result[4];
static char dns_query_name[NET_DNS_HOSTNAME_MAX + 1U];
static uint32_t dns_query_ttl;

struct net_dns_cache_entry {
    bool valid;
    char hostname[NET_DNS_HOSTNAME_MAX + 1U];
    uint8_t ipv4[4];
    uint64_t expires_at;
};

static struct net_dns_cache_entry dns_cache[NET_DNS_CACHE_SIZE];
static size_t dns_cache_replace;
static uint8_t rx_frame[NET_MAX_FRAME];

struct net_tcp_reorder_entry {
    bool valid;
    uint32_t sequence;
    uint16_t payload_length;
    bool fin;
    uint8_t payload[NET_TCP_REORDER_PAYLOAD_MAX];
};

struct net_tcp_connection {
    bool active;
    bool connecting;
    bool connected;
    bool failed;
    bool peer_closed;
    bool ack_waiting;
    bool ack_received;
    uint32_t ack_expected;
    uint64_t syn_last_send_tick;
    uint32_t syn_retry_count;
    uint8_t remote_ipv4[4];
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t local_sequence;
    uint32_t remote_sequence;
    uint8_t receive_buffer[NET_TCP_RX_CAPACITY];
    size_t receive_length;
    size_t reorder_length;
    struct net_tcp_reorder_entry reorder[NET_TCP_REORDER_SLOTS];
};
static struct net_tcp_connection tcp_connection;
static uint16_t tcp_next_port = 49152U;

static bool ipv4_next_hop(const uint8_t destination[4], uint8_t next_hop[4]);
static void tcp_retry_pending_syn(void);

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
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
    return netdev_send_frame(frame, NET_ETHERNET_HEADER + length);
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

static void receive_dhcp(const uint8_t *packet, size_t length,
                        const uint8_t source_ip[4]);
static void receive_dns(const uint8_t *packet, size_t length,
                        const uint8_t source_ip[4]);
static void receive_tcp(const uint8_t *segment, size_t length,
                        const uint8_t source_ip[4],
                        const uint8_t destination_ip[4]);

static void receive_ipv4(const uint8_t *ip, size_t length) {
    if (length < NET_IPV4_HEADER || (ip[0] >> 4) != 4U) return;
    const size_t header_length = (size_t)(ip[0] & 0x0FU) * 4U;
    const uint16_t total_length = read_be16(ip + 2U);
    if (header_length < NET_IPV4_HEADER || header_length > length ||
        total_length < header_length || total_length > length ||
        checksum(ip, header_length) != 0U) return;

    /*
     * Fragment reassembly is not implemented yet. Never pass a partial
     * datagram to UDP/TCP: reject the reserved flag, MF, and non-zero
     * fragment offsets while allowing the Don't Fragment flag.
     */
    const uint16_t fragment_field = read_be16(ip + 6U);
    if ((fragment_field & 0xBFFFU) != 0U) return;

    const uint8_t *transport = ip + header_length;
    const size_t transport_length = total_length - header_length;
    if (ip[9] == IP_PROTOCOL_UDP) {
        if (transport_length < 8U) return;
        const uint16_t source_port = read_be16(transport);
        const uint16_t destination_port = read_be16(transport + 2U);
        const uint16_t udp_length = read_be16(transport + 4U);
        if (udp_length < 8U || udp_length > transport_length) return;
        if (source_port == NET_DHCP_SERVER_PORT &&
            destination_port == NET_DHCP_CLIENT_PORT) {
            receive_dhcp(transport + 8U, udp_length - 8U, ip + 12U);
        } else if (source_port == NET_DNS_SERVER_PORT &&
                   destination_port == NET_DNS_CLIENT_PORT) {
            receive_dns(transport + 8U, udp_length - 8U, ip + 12U);
        }
    } else if (ip[9] == IP_PROTOCOL_TCP) {
        receive_tcp(transport, transport_length, ip + 12U, ip + 16U);
    }
}


static bool append_dhcp_option(uint8_t *packet, size_t capacity,
                               size_t *cursor, uint8_t code,
                               const uint8_t *value, uint8_t length) {
    if (packet == NULL || cursor == NULL ||
        (length != 0U && value == NULL) ||
        *cursor > capacity || capacity - *cursor < (size_t)length + 2U) {
        return false;
    }
    packet[(*cursor)++] = code;
    packet[(*cursor)++] = length;
    for (size_t i = 0; i < length; ++i) packet[(*cursor)++] = value[i];
    return true;
}

static bool send_ipv4_udp_raw(const uint8_t source_ip[4],
                              const uint8_t destination_ip[4],
                              const uint8_t destination_mac[6],
                              uint16_t source_port, uint16_t destination_port,
                              const void *payload, size_t payload_length) {
    if (source_ip == NULL || destination_ip == NULL ||
        destination_mac == NULL || (payload_length != 0U && payload == NULL) ||
        payload_length > 1400U) return false;

    uint8_t packet[NET_IPV4_HEADER + 8U + 1400U];
    const size_t udp_length = 8U + payload_length;
    const size_t ip_length = NET_IPV4_HEADER + udp_length;
    packet[0] = 0x45U;
    packet[1] = 0U;
    write_be16(packet + 2U, (uint16_t)ip_length);
    write_be16(packet + 4U, network.ipv4_id++);
    write_be16(packet + 6U, 0U);
    packet[8] = 64U;
    packet[9] = IP_PROTOCOL_UDP;
    write_be16(packet + 10U, 0U);
    copy_bytes(packet + 12U, source_ip, 4U);
    copy_bytes(packet + 16U, destination_ip, 4U);
    write_be16(packet + 10U, checksum(packet, NET_IPV4_HEADER));

    uint8_t *udp = packet + NET_IPV4_HEADER;
    write_be16(udp, source_port);
    write_be16(udp + 2U, destination_port);
    write_be16(udp + 4U, (uint16_t)udp_length);
    write_be16(udp + 6U, 0U);
    if (payload_length != 0U) copy_bytes(udp + 8U, payload, payload_length);
    return send_ethernet(destination_mac, ETHERTYPE_IPV4, packet, ip_length);
}

static bool send_dhcp_message(uint8_t message_type) {
    uint8_t packet[NET_DHCP_PACKET_SIZE] = {0};
    packet[0] = 1U;
    packet[1] = 1U;
    packet[2] = 6U;
    write_be32(packet + 4U, dhcp_transaction_id);
    packet[10] = 0x80U;
    copy_bytes(packet + 28U, network.mac, 6U);
    packet[236] = 99U;
    packet[237] = 130U;
    packet[238] = 83U;
    packet[239] = 99U;

    size_t cursor = NET_DHCP_OPTIONS_OFFSET;
    const uint8_t type_option[1] = {message_type};
    const uint8_t request_list[4] = {1U, 3U, 6U, 51U};
    const uint8_t maximum_size[2] = {0x02U, 0x40U};
    const uint8_t hostname[5] = {'N', 'E', 'X', 'U', 'S'};
    if (!append_dhcp_option(packet, sizeof(packet), &cursor,
                            53U, type_option, 1U)) return false;

    if (message_type == 3U) {
        if (!append_dhcp_option(packet, sizeof(packet), &cursor,
                                50U, offered_ipv4, 4U) ||
            !append_dhcp_option(packet, sizeof(packet), &cursor,
                                54U, dhcp_server_ipv4, 4U)) return false;
    }

    if (!append_dhcp_option(packet, sizeof(packet), &cursor,
                            55U, request_list, sizeof(request_list)) ||
        !append_dhcp_option(packet, sizeof(packet), &cursor,
                            57U, maximum_size, sizeof(maximum_size)) ||
        !append_dhcp_option(packet, sizeof(packet), &cursor,
                            12U, hostname, sizeof(hostname)) ||
        cursor >= sizeof(packet)) return false;
    packet[cursor++] = 255U;

    static const uint8_t zero_ip[4] = {0U, 0U, 0U, 0U};
    static const uint8_t broadcast_ip[4] = {255U, 255U, 255U, 255U};
    static const uint8_t broadcast_mac[6] =
        {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};

    const bool sent = send_ipv4_udp_raw(zero_ip, broadcast_ip, broadcast_mac,
                                        NET_DHCP_CLIENT_PORT,
                                        NET_DHCP_SERVER_PORT,
                                        packet, cursor);
    if (sent) dhcp_last_send_tick = timer_ticks();
    return sent;
}

static bool read_dhcp_options(const uint8_t *packet, size_t length,
                              uint8_t *message_type) {
    if (packet == NULL || message_type == NULL ||
        length < NET_DHCP_OPTIONS_OFFSET ||
        read_be32(packet + 236U) != 0x63825363U) return false;

    *message_type = 0U;
    size_t cursor = NET_DHCP_OPTIONS_OFFSET;
    while (cursor < length) {
        const uint8_t code = packet[cursor++];
        if (code == 0U) continue;
        if (code == 255U) break;
        if (cursor >= length) return false;
        const uint8_t option_length = packet[cursor++];
        if ((size_t)option_length > length - cursor) return false;
        const uint8_t *value = packet + cursor;
        if (code == 53U && option_length == 1U) {
            *message_type = value[0];
        } else if (code == 1U && option_length == 4U) {
            copy_bytes(subnet_mask, value, 4U);
        } else if (code == 3U && option_length >= 4U) {
            copy_bytes(gateway_ipv4, value, 4U);
        } else if (code == 6U && option_length >= 4U) {
            copy_bytes(dns_ipv4, value, 4U);
        } else if (code == 54U && option_length == 4U) {
            copy_bytes(dhcp_server_ipv4, value, 4U);
        }
        cursor += option_length;
    }
    return *message_type != 0U;
}

static void receive_dhcp(const uint8_t *packet, size_t length,
                         const uint8_t source_ip[4]) {
    if (packet == NULL || source_ip == NULL ||
        length < NET_DHCP_OPTIONS_OFFSET ||
        packet[0] != 2U || packet[1] != 1U || packet[2] != 6U ||
        read_be32(packet + 4U) != dhcp_transaction_id ||
        !equal_bytes(packet + 28U, network.mac, 6U)) return;

    uint8_t message_type = 0U;
    if (!read_dhcp_options(packet, length, &message_type)) return;

    if (message_type == 2U && dhcp_state == NET_DHCP_DISCOVERING) {
        copy_bytes(offered_ipv4, packet + 16U, 4U);
        if (ipv4_zero(offered_ipv4)) return;
        if (ipv4_zero(dhcp_server_ipv4)) {
            copy_bytes(dhcp_server_ipv4, source_ip, 4U);
        }
        dhcp_state = NET_DHCP_REQUESTING;
        dhcp_retry_count = 0U;
        if (send_dhcp_message(3U)) {
            serial_write("NET: DHCP offer received; request sent\r\n");
        }
        return;
    }

    if (message_type == 5U && dhcp_state == NET_DHCP_REQUESTING) {
        copy_bytes(network.ipv4, packet + 16U, 4U);
        if (ipv4_zero(network.ipv4)) copy_bytes(network.ipv4, offered_ipv4, 4U);
        dhcp_state = NET_DHCP_BOUND;
        serial_write("NET: DHCP bound IPv4=");
        for (size_t i = 0; i < 4U; ++i) {
            serial_write_hex(network.ipv4[i]);
            if (i != 3U) serial_write(".");
        }
        serial_write("\r\nNEXUS DHCP READY\r\n");
        return;
    }

    if (message_type == 6U) {
        dhcp_state = NET_DHCP_DISCOVERING;
        dhcp_retry_count = 0U;
        (void)send_dhcp_message(1U);
    }
}

static void dhcp_retry(void) {
    if (dhcp_state == NET_DHCP_IDLE || dhcp_state == NET_DHCP_BOUND) return;
    if (timer_ticks() - dhcp_last_send_tick < NET_DHCP_RETRY_TICKS) return;
    if (dhcp_retry_count >= NET_DHCP_MAX_RETRIES) {
        dhcp_state = NET_DHCP_DISCOVERING;
        dhcp_retry_count = 0U;
    }
    ++dhcp_retry_count;
    const uint8_t message_type =
        dhcp_state == NET_DHCP_REQUESTING ? 3U : 1U;
    (void)send_dhcp_message(message_type);
}



static bool dns_name_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL) return false;
    size_t i = 0U;
    while (left[i] != '\0' && right[i] != '\0') {
        char a = left[i];
        char b = right[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
        ++i;
    }
    return left[i] == '\0' && right[i] == '\0';
}

/* Convert a user-supplied DNS name to a bounded, lowercase ASCII hostname. */
static bool dns_normalize_hostname(const char *hostname,
                                   char output[NET_DNS_HOSTNAME_MAX + 1U]) {
    if (hostname == NULL || output == NULL) return false;
    size_t length = 0U;
    while (hostname[length] != '\0') {
        if (length >= NET_DNS_HOSTNAME_MAX + 1U) return false;
        ++length;
    }
    if (length != 0U && hostname[length - 1U] == '.') --length;
    if (length == 0U || length > NET_DNS_HOSTNAME_MAX) return false;

    size_t label_length = 0U;
    for (size_t i = 0U; i < length; ++i) {
        char ch = hostname[i];
        if (ch == '.') {
            if (label_length == 0U || output[i - 1U] == '-') return false;
            output[i] = '.';
            label_length = 0U;
            continue;
        }
        const bool alpha = (ch >= 'a' && ch <= 'z') ||
                           (ch >= 'A' && ch <= 'Z');
        const bool digit = ch >= '0' && ch <= '9';
        if (!alpha && !digit && ch != '-') return false;
        if (label_length == 0U && ch == '-') return false;
        if (++label_length > 63U) return false;
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        output[i] = ch;
    }
    if (label_length == 0U || output[length - 1U] == '-') return false;
    output[length] = '\0';
    return true;
}

static void dns_cache_clear(void) {
    for (size_t i = 0U; i < NET_DNS_CACHE_SIZE; ++i)
        dns_cache[i].valid = false;
    dns_cache_replace = 0U;
}

static bool dns_cache_lookup(const char *hostname, uint8_t result[4]) {
    if (hostname == NULL || result == NULL) return false;
    const uint64_t now = timer_ticks();
    for (size_t i = 0U; i < NET_DNS_CACHE_SIZE; ++i) {
        struct net_dns_cache_entry *entry = &dns_cache[i];
        if (!entry->valid) continue;
        if (now >= entry->expires_at) {
            entry->valid = false;
            continue;
        }
        if (dns_name_equal(entry->hostname, hostname)) {
            copy_bytes(result, entry->ipv4, 4U);
            return true;
        }
    }
    return false;
}

static void dns_cache_store(const char *hostname, const uint8_t address[4],
                            uint32_t ttl_seconds) {
    if (hostname == NULL || address == NULL || ttl_seconds == 0U) return;
    if (ipv4_zero(address)) return;
    if (ttl_seconds > NET_DNS_MAX_TTL_SECONDS)
        ttl_seconds = NET_DNS_MAX_TTL_SECONDS;

    size_t slot = NET_DNS_CACHE_SIZE;
    for (size_t i = 0U; i < NET_DNS_CACHE_SIZE; ++i) {
        if (dns_cache[i].valid &&
            dns_name_equal(dns_cache[i].hostname, hostname)) {
            slot = i;
            break;
        }
    }
    if (slot == NET_DNS_CACHE_SIZE) {
        slot = dns_cache_replace;
        dns_cache_replace = (dns_cache_replace + 1U) % NET_DNS_CACHE_SIZE;
    }

    size_t length = 0U;
    while (hostname[length] != '\0' &&
           length < NET_DNS_HOSTNAME_MAX) ++length;
    if (length == 0U || length > NET_DNS_HOSTNAME_MAX) return;

    struct net_dns_cache_entry *entry = &dns_cache[slot];
    for (size_t i = 0U; i < length; ++i)
        entry->hostname[i] = hostname[i];
    entry->hostname[length] = '\0';
    copy_bytes(entry->ipv4, address, 4U);
    const uint64_t now = timer_ticks();
    const uint64_t ttl_ticks =
        (uint64_t)ttl_seconds * NET_DNS_TICKS_PER_SECOND;
    entry->expires_at = UINT64_MAX - now < ttl_ticks
        ? UINT64_MAX : now + ttl_ticks;
    entry->valid = true;
}

/*
 * Decode a DNS name, following only bounded backward compression pointers.
 * The caller's offset advances over the encoded name, not its pointed-to
 * expansion. This rejects forward pointers, loops, invalid labels and
 * truncated names before any record is consumed.
 */
static bool dns_decode_name(const uint8_t *packet, size_t length,
                            size_t *offset, char *output,
                            size_t output_capacity) {
    if (packet == NULL || offset == NULL || output == NULL ||
        output_capacity == 0U || *offset >= length) return false;

    size_t cursor = *offset;
    size_t resume = SIZE_MAX;
    size_t output_length = 0U;
    unsigned int pointer_hops = 0U;
    unsigned int labels = 0U;

    for (;;) {
        if (cursor >= length) return false;
        const size_t marker = cursor;
        const uint8_t label = packet[cursor++];
        if ((label & 0xC0U) == 0xC0U) {
            if (cursor >= length) return false;
            const size_t target =
                ((size_t)(label & 0x3FU) << 8) | packet[cursor++];
            if (target >= marker || target >= length ||
                ++pointer_hops > 16U) return false;
            if (resume == SIZE_MAX) resume = cursor;
            cursor = target;
            continue;
        }
        if ((label & 0xC0U) != 0U || label > 63U) return false;
        if (label == 0U) {
            if (resume == SIZE_MAX) *offset = cursor;
            else *offset = resume;
            output[output_length] = '\0';
            return true;
        }
        if (++labels > 127U || (size_t)label > length - cursor)
            return false;
        if (output_length != 0U) {
            if (output_length + 1U >= output_capacity) return false;
            output[output_length++] = '.';
        }
        if ((size_t)label > output_capacity - output_length - 1U)
            return false;
        for (size_t i = 0U; i < (size_t)label; ++i) {
            char ch = (char)packet[cursor + i];
            const bool alpha = (ch >= 'a' && ch <= 'z') ||
                               (ch >= 'A' && ch <= 'Z');
            const bool digit = ch >= '0' && ch <= '9';
            if (!alpha && !digit && ch != '-') return false;
            if (ch >= 'A' && ch <= 'Z')
                ch = (char)(ch - 'A' + 'a');
            output[output_length++] = ch;
        }
        cursor += (size_t)label;
    }
}

/* Parse all answer records and find an A or CNAME for the requested owner. */
static bool dns_find_answer(const uint8_t *packet, size_t length,
                            size_t answer_offset, uint16_t answer_count,
                            const char *wanted_name, uint16_t wanted_type,
                            char *cname_target, uint8_t address[4],
                            uint32_t *ttl, bool *found) {
    if (packet == NULL || wanted_name == NULL || ttl == NULL ||
        found == NULL || (wanted_type != 1U && wanted_type != 5U))
        return false;
    *found = false;
    size_t cursor = answer_offset;

    for (uint16_t i = 0U; i < answer_count; ++i) {
        char owner[NET_DNS_HOSTNAME_MAX + 1U];
        if (!dns_decode_name(packet, length, &cursor,
                             owner, sizeof(owner)) ||
            cursor > length || length - cursor < 10U) return false;

        const uint16_t type = read_be16(packet + cursor);
        const uint16_t klass = read_be16(packet + cursor + 2U);
        const uint32_t record_ttl = read_be32(packet + cursor + 4U);
        const uint16_t data_length = read_be16(packet + cursor + 8U);
        cursor += 10U;
        if ((size_t)data_length > length - cursor) return false;
        const size_t data_start = cursor;
        const size_t data_end = cursor + (size_t)data_length;

        if (klass == 1U && type == wanted_type &&
            dns_name_equal(owner, wanted_name)) {
            if (wanted_type == 1U) {
                if (data_length != 4U || address == NULL) return false;
                if (!*found) {
                    copy_bytes(address, packet + data_start, 4U);
                    *ttl = record_ttl;
                    *found = true;
                } else if (record_ttl < *ttl) {
                    *ttl = record_ttl;
                }
            } else {
                if (cname_target == NULL || data_length == 0U) return false;
                size_t name_cursor = data_start;
                char target[NET_DNS_HOSTNAME_MAX + 1U];
                if (!dns_decode_name(packet, length, &name_cursor,
                                     target, sizeof(target)) ||
                    name_cursor != data_end) return false;
                if (!*found) {
                    size_t j = 0U;
                    while (target[j] != '\0') {
                        cname_target[j] = target[j];
                        ++j;
                    }
                    cname_target[j] = '\0';
                    *ttl = record_ttl;
                    *found = true;
                } else {
                    if (!dns_name_equal(cname_target, target)) return false;
                    if (record_ttl < *ttl) *ttl = record_ttl;
                }
            }
        }
        cursor = data_end;
    }
    return true;
}

static void receive_dns(const uint8_t *packet, size_t length,
                        const uint8_t source_ip[4]) {
    if (!dns_query_active || packet == NULL || source_ip == NULL ||
        length < 12U || !equal_bytes(source_ip, dns_ipv4, 4U) ||
        read_be16(packet) != dns_query_id) return;

    const uint16_t flags = read_be16(packet + 2U);
    if ((flags & 0x8000U) == 0U || (flags & 0x7800U) != 0U ||
        (flags & 0x0200U) != 0U) return;
    const uint16_t questions = read_be16(packet + 4U);
    const uint16_t answers = read_be16(packet + 6U);
    if (questions != 1U) {
        dns_query_done = true;
        dns_query_success = false;
        return;
    }

    size_t cursor = 12U;
    char question_name[NET_DNS_HOSTNAME_MAX + 1U];
    if (!dns_decode_name(packet, length, &cursor,
                         question_name, sizeof(question_name)) ||
        cursor > length || length - cursor < 4U) {
        dns_query_done = true;
        dns_query_success = false;
        return;
    }
    const uint16_t question_type = read_be16(packet + cursor);
    const uint16_t question_class = read_be16(packet + cursor + 2U);
    cursor += 4U;

    /* Ignore unrelated packets sharing an ID; keep waiting for our answer. */
    if (!dns_name_equal(question_name, dns_query_name) ||
        question_type != 1U || question_class != 1U) return;
    if ((flags & 0x000FU) != 0U) {
        dns_query_done = true;
        dns_query_success = false;
        return;
    }

    char current[NET_DNS_HOSTNAME_MAX + 1U];
    char visited[NET_DNS_MAX_CNAME_HOPS][NET_DNS_HOSTNAME_MAX + 1U];
    size_t current_length = 0U;
    while (dns_query_name[current_length] != '\0') {
        current[current_length] = dns_query_name[current_length];
        ++current_length;
    }
    current[current_length] = '\0';
    uint32_t effective_ttl = UINT32_MAX;

    for (size_t hop = 0U; hop < NET_DNS_MAX_CNAME_HOPS; ++hop) {
        for (size_t prior = 0U; prior < hop; ++prior) {
            if (dns_name_equal(visited[prior], current)) {
                dns_query_done = true;
                dns_query_success = false;
                return;
            }
        }
        size_t copy = 0U;
        while (current[copy] != '\0') {
            visited[hop][copy] = current[copy];
            ++copy;
        }
        visited[hop][copy] = '\0';

        uint8_t address[4];
        uint32_t address_ttl = 0U;
        bool have_address = false;
        if (!dns_find_answer(packet, length, cursor, answers, current, 1U,
                             NULL, address, &address_ttl, &have_address)) {
            dns_query_done = true;
            dns_query_success = false;
            return;
        }
        if (have_address) {
            if (address_ttl < effective_ttl)
                effective_ttl = address_ttl;
            copy_bytes(dns_query_result, address, 4U);
            dns_query_ttl = effective_ttl;
            dns_query_success = true;
            dns_query_done = true;
            return;
        }

        char target[NET_DNS_HOSTNAME_MAX + 1U];
        uint32_t cname_ttl = 0U;
        bool have_cname = false;
        if (!dns_find_answer(packet, length, cursor, answers, current, 5U,
                             target, NULL, &cname_ttl, &have_cname)) {
            dns_query_done = true;
            dns_query_success = false;
            return;
        }
        if (!have_cname) break;
        if (cname_ttl < effective_ttl) effective_ttl = cname_ttl;
        copy = 0U;
        while (target[copy] != '\0') {
            current[copy] = target[copy];
            ++copy;
        }
        current[copy] = '\0';
    }

    dns_query_done = true;
    dns_query_success = false;
}

bool net_dns_resolve(const char *hostname, uint8_t result[4],
                     uint64_t timeout_ticks) {
    if (!network.ready || dhcp_state != NET_DHCP_BOUND ||
        hostname == NULL || result == NULL || timeout_ticks == 0U ||
        ipv4_zero(dns_ipv4)) return false;

    char normalized[NET_DNS_HOSTNAME_MAX + 1U];
    if (!dns_normalize_hostname(hostname, normalized)) return false;
    if (dns_cache_lookup(normalized, result)) return true;

    size_t name_length = 0U;
    while (normalized[name_length] != '\0') {
        dns_query_name[name_length] = normalized[name_length];
        ++name_length;
    }
    dns_query_name[name_length] = '\0';
    dns_query_ttl = 0U;

    uint8_t query[NET_DNS_PACKET_SIZE];
    for (size_t i = 0U; i < sizeof(query); ++i) query[i] = 0U;

    dns_query_id = ++dns_next_id;
    if (dns_query_id == 0U) dns_query_id = ++dns_next_id;
    write_be16(query, dns_query_id);
    write_be16(query + 2U, 0x0100U); /* recursion desired */
    write_be16(query + 4U, 1U);      /* one question */

    size_t cursor = 12U;
    size_t input = 0U;
    while (input < name_length) {
        size_t label_length = 0U;
        while (input + label_length < name_length &&
               normalized[input + label_length] != '.') ++label_length;
        if (label_length == 0U || label_length > 63U ||
            cursor + label_length + 1U >= sizeof(query)) return false;
        query[cursor++] = (uint8_t)label_length;
        for (size_t i = 0U; i < label_length; ++i)
            query[cursor++] = (uint8_t)normalized[input + i];
        input += label_length;
        if (input < name_length) ++input;
    }
    if (cursor + 5U > sizeof(query)) return false;
    query[cursor++] = 0U;
    write_be16(query + cursor, 1U); cursor += 2U; /* A */
    write_be16(query + cursor, 1U); cursor += 2U; /* IN */

    dns_query_active = true;
    dns_query_done = false;
    dns_query_success = false;

    const uint64_t start = timer_ticks();
    uint8_t neighbor[6];
    uint8_t next_hop[4];
    uint64_t arp_budget = timeout_ticks / 2U;
    if (arp_budget == 0U) arp_budget = 1U;
    if (!ipv4_next_hop(dns_ipv4, next_hop)) {
        dns_query_active = false;
        return false;
    }
    bool arp_probe_sent = false;
    uint64_t last_arp_tick = 0U;
    while (!arp_lookup(next_hop, neighbor) &&
           timer_ticks() - start < arp_budget) {
        const uint64_t now = timer_ticks();
        if (!arp_probe_sent || now - last_arp_tick >= NET_ARP_RETRY_TICKS) {
            (void)send_arp_request(next_hop);
            last_arp_tick = now;
            arp_probe_sent = true;
        }
        net_poll();
        __asm__ volatile ("pause");
    }
    if (!arp_lookup(next_hop, neighbor)) {
        dns_query_active = false;
        return false;
    }

    if (!net_udp_send(dns_ipv4, NET_DNS_CLIENT_PORT,
                      NET_DNS_SERVER_PORT, query, (uint16_t)cursor)) {
        dns_query_active = false;
        return false;
    }

    const uint64_t query_start = timer_ticks();
    uint64_t last_send_tick = query_start;
    uint64_t retry_interval = NET_DNS_RETRY_INITIAL_TICKS;
    unsigned int retry_count = 0U;
    while (!dns_query_done) {
        const uint64_t now = timer_ticks();
        if (now - query_start >= timeout_ticks) break;
        if (retry_count < NET_DNS_MAX_RETRIES &&
            now - last_send_tick >= retry_interval) {
            /*
             * DNS uses the same transaction ID for retransmissions so a late
             * answer from an earlier attempt remains valid. Retry intervals
             * back off (1s, then 2s) and stay inside the caller's deadline.
             */
            (void)net_udp_send(dns_ipv4, NET_DNS_CLIENT_PORT,
                               NET_DNS_SERVER_PORT, query, (uint16_t)cursor);
            ++retry_count;
            last_send_tick = now;
            if (retry_interval <= UINT64_MAX / 2U)
                retry_interval *= 2U;
        }
        net_poll();
        __asm__ volatile ("pause");
    }
    dns_query_active = false;
    if (!dns_query_done || !dns_query_success) return false;
    copy_bytes(result, dns_query_result, 4U);
    dns_cache_store(dns_query_name, result, dns_query_ttl);
    return true;
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
    for (size_t i = 0; i < 4U; ++i) {
        offered_ipv4[i] = 0U;
        dhcp_server_ipv4[i] = 0U;
        subnet_mask[i] = 0U;
        gateway_ipv4[i] = 0U;
        dns_ipv4[i] = 0U;
    }
    dhcp_state = NET_DHCP_IDLE;
    dhcp_transaction_id = 0U;
    dhcp_retry_count = 0U;
    dhcp_last_send_tick = 0U;
    dns_query_active = false;
    dns_query_done = false;
    dns_query_success = false;
    dns_query_ttl = 0U;
    dns_query_name[0] = '\0';
    dns_next_id = 0x4E58U;
    dns_cache_clear();
    struct nexus_netdev_info device_info;
    if (!netdev_get_info(&device_info)) return false;
    copy_bytes(network.mac, device_info.mac, sizeof(network.mac));
    copy_bytes(network.interface_name, device_info.name,
               sizeof(network.interface_name));
    network.interface_medium = device_info.medium;
    network.ready = true;
    network.ipv4_id = 1U;
    network.link_up = netdev_link_up();
    dhcp_state = NET_DHCP_DISCOVERING;
    dhcp_transaction_id = 0x4E585553U;
    dhcp_retry_count = 0U;
    dhcp_last_send_tick = timer_ticks();
    serial_write("NEXUS NETWORK DEVICE READY: ");
    serial_write(device_info.name);
    serial_write("\r\n");
    if (network.link_up) {
        if (send_dhcp_message(1U)) {
            serial_write("NEXUS DHCP DISCOVER SENT\r\n");
        } else {
            serial_write("WARNING: DHCP Discover transmit failed\r\n");
        }
    } else {
        serial_write("NEXUS NETWORK WAITING FOR PHYSICAL LINK\r\n");
    }
    return true;
}

static void network_handle_link_change(bool link_up) {
    if (network.link_up == link_up) return;
    network.link_up = link_up;
    for (size_t i = 0U; i < NET_ARP_CACHE_SIZE; ++i)
        network.arp[i].valid = false;
    network.arp_replace = 0U;
    tcp_connection = (struct net_tcp_connection){0};
    dns_query_active = false;
    dns_query_done = true;
    dns_query_success = false;
    dns_cache_clear();

    if (!link_up) {
        for (size_t i = 0U; i < 4U; ++i) {
            network.ipv4[i] = 0U;
            offered_ipv4[i] = 0U;
            dhcp_server_ipv4[i] = 0U;
            subnet_mask[i] = 0U;
            gateway_ipv4[i] = 0U;
            dns_ipv4[i] = 0U;
        }
        dhcp_state = NET_DHCP_DISCOVERING;
        dhcp_retry_count = 0U;
        serial_write("NEXUS NETWORK LINK DOWN; CLEARED STALE STATE\r\n");
        return;
    }

    dhcp_state = NET_DHCP_DISCOVERING;
    dhcp_transaction_id ^= (uint32_t)timer_ticks() ^ 0x4E585553U;
    if (dhcp_transaction_id == 0U) dhcp_transaction_id = 0x4E585553U;
    dhcp_retry_count = 0U;
    dhcp_last_send_tick = timer_ticks();
    serial_write("NEXUS NETWORK LINK UP; RESTARTING DHCP\r\n");
    if (send_dhcp_message(1U))
        serial_write("NEXUS DHCP DISCOVER SENT\r\n");
}

void net_poll(void) {
    if (!network.ready) return;
    const bool link_up = netdev_link_up();
    struct nexus_netdev_info device_info;
    if (!netdev_get_info(&device_info)) return;
    if (!equal_bytes(device_info.mac, network.mac, sizeof(network.mac)) ||
        device_info.medium != network.interface_medium) {
        copy_bytes(network.mac, device_info.mac, sizeof(network.mac));
        copy_bytes(network.interface_name, device_info.name,
                   sizeof(network.interface_name));
        network.interface_medium = device_info.medium;
        for (size_t i = 0U; i < 4U; ++i) {
            network.ipv4[i] = 0U;
            offered_ipv4[i] = 0U;
            dhcp_server_ipv4[i] = 0U;
            subnet_mask[i] = 0U;
            gateway_ipv4[i] = 0U;
            dns_ipv4[i] = 0U;
        }
        for (size_t i = 0U; i < NET_ARP_CACHE_SIZE; ++i)
            network.arp[i].valid = false;
        network.arp_replace = 0U;
        tcp_connection = (struct net_tcp_connection){0};
        dns_query_active = false;
        dns_query_done = true;
        dns_query_success = false;
        dns_cache_clear();
        /* Force the existing link transition path to reacquire DHCP. */
        network.link_up = !link_up;
        serial_write("NEXUS NETWORK ADAPTER SELECTED: ");
        serial_write(device_info.name);
        serial_write("\r\n");
    }
    network_handle_link_change(link_up);
    if (!network.link_up) return;
    for (size_t i = 0; i < 32U; ++i) {
        size_t length = 0U;
        if (!netdev_receive_frame(rx_frame, sizeof(rx_frame), &length)) break;
        receive_frame(rx_frame, length);
    }
    tcp_retry_pending_syn();
    dhcp_retry();
}

bool net_get_info(struct nexus_net_info *info) {
    if (info == NULL || !network.ready) return false;
    info->interface_ready = true;
    info->link_up = network.link_up;
    info->ipv4_configured = dhcp_state == NET_DHCP_BOUND;
    copy_bytes(info->interface_name, network.interface_name,
               sizeof(info->interface_name));
    copy_bytes(info->mac, network.mac, sizeof(info->mac));
    copy_bytes(info->ipv4, network.ipv4, sizeof(info->ipv4));
    copy_bytes(info->subnet_mask, subnet_mask, sizeof(info->subnet_mask));
    copy_bytes(info->gateway, gateway_ipv4, sizeof(info->gateway));
    copy_bytes(info->dns_server, dns_ipv4, sizeof(info->dns_server));
    return true;
}

static bool ipv4_same_subnet(const uint8_t left[4],
                             const uint8_t right[4]) {
    bool mask_known = false;
    bool same = true;
    for (size_t i = 0; i < 4U; ++i) {
        if (subnet_mask[i] != 0U) mask_known = true;
        if ((left[i] & subnet_mask[i]) != (right[i] & subnet_mask[i]))
            same = false;
    }
    /* Without a subnet mask, only the local address is known to be on-link. */
    return mask_known ? same : equal_bytes(left, right, 4U);
}

static bool ipv4_next_hop(const uint8_t destination[4], uint8_t next_hop[4]) {
    if (destination == NULL || next_hop == NULL) return false;
    if (ipv4_same_subnet(network.ipv4, destination)) {
        copy_bytes(next_hop, destination, 4U);
        return true;
    }
    if (ipv4_zero(gateway_ipv4)) return false;
    copy_bytes(next_hop, gateway_ipv4, 4U);
    return true;
}

static bool ipv4_is_limited_broadcast(const uint8_t address[4]) {
    return address[0] == 255U && address[1] == 255U &&
           address[2] == 255U && address[3] == 255U;
}

bool net_udp_send(const uint8_t destination_ipv4[4],
                  uint16_t source_port, uint16_t destination_port,
                  const void *payload, uint16_t payload_length) {
    if (!network.ready || dhcp_state != NET_DHCP_BOUND ||
        destination_ipv4 == NULL ||
        (payload_length != 0U && payload == NULL) || payload_length > 1400U) {
        return false;
    }
    uint8_t destination_mac[6];
    static const uint8_t broadcast_mac[6] =
        {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
    if (ipv4_is_limited_broadcast(destination_ipv4)) {
        copy_bytes(destination_mac, broadcast_mac, 6U);
    } else {
        uint8_t next_hop[4];
        if (!ipv4_next_hop(destination_ipv4, next_hop)) return false;
        if (!arp_lookup(next_hop, destination_mac)) {
            (void)send_arp_request(next_hop);
            return false;
        }
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

static uint16_t tcp_checksum_value(const uint8_t source_ip[4],
                                   const uint8_t destination_ip[4],
                                   const uint8_t *segment, size_t length) {
    uint8_t pseudo[NET_MAX_FRAME];
    if (source_ip == NULL || destination_ip == NULL || segment == NULL ||
        length > sizeof(pseudo) - 12U) return 0U;
    copy_bytes(pseudo, source_ip, 4U);
    copy_bytes(pseudo + 4U, destination_ip, 4U);
    pseudo[8] = 0U;
    pseudo[9] = IP_PROTOCOL_TCP;
    write_be16(pseudo + 10U, (uint16_t)length);
    copy_bytes(pseudo + 12U, segment, length);
    return checksum(pseudo, length + 12U);
}

static bool send_tcp_segment(uint8_t flags, const void *payload,
                             size_t payload_length) {
    if (!tcp_connection.active || payload_length > NET_TCP_MAX_PAYLOAD ||
        (payload_length != 0U && payload == NULL)) return false;
    uint8_t next_hop[4];
    uint8_t destination_mac[6];
    if (!ipv4_next_hop(tcp_connection.remote_ipv4, next_hop) ||
        !arp_lookup(next_hop, destination_mac)) return false;

    uint8_t packet[NET_IPV4_HEADER + NET_TCP_HEADER + NET_TCP_MAX_PAYLOAD];
    const size_t tcp_length = NET_TCP_HEADER + payload_length;
    const size_t ip_length = NET_IPV4_HEADER + tcp_length;
    packet[0] = 0x45U;
    packet[1] = 0U;
    write_be16(packet + 2U, (uint16_t)ip_length);
    write_be16(packet + 4U, network.ipv4_id++);
    write_be16(packet + 6U, 0U);
    packet[8] = 64U;
    packet[9] = IP_PROTOCOL_TCP;
    write_be16(packet + 10U, 0U);
    copy_bytes(packet + 12U, network.ipv4, 4U);
    copy_bytes(packet + 16U, tcp_connection.remote_ipv4, 4U);
    write_be16(packet + 10U, checksum(packet, NET_IPV4_HEADER));

    uint8_t *tcp = packet + NET_IPV4_HEADER;
    write_be16(tcp, tcp_connection.local_port);
    write_be16(tcp + 2U, tcp_connection.remote_port);
    write_be32(tcp + 4U, tcp_connection.local_sequence);
    write_be32(tcp + 8U, tcp_connection.remote_sequence);
    tcp[12] = 0x50U;
    tcp[13] = flags;
    write_be16(tcp + 14U, (uint16_t)NET_TCP_RX_CAPACITY);
    write_be16(tcp + 16U, 0U);
    write_be16(tcp + 18U, 0U);
    if (payload_length != 0U)
        copy_bytes(tcp + NET_TCP_HEADER, payload, payload_length);
    write_be16(tcp + 16U,
               tcp_checksum_value(network.ipv4, tcp_connection.remote_ipv4,
                                  tcp, tcp_length));
    return send_ethernet(destination_mac, ETHERTYPE_IPV4, packet, ip_length);
}

static unsigned int tcp_https_trace_events;

static void tcp_https_trace(const char *stage, const uint8_t *segment,
                            size_t length) {
    if (tcp_connection.remote_port != 443U ||
        tcp_https_trace_events >= 96U) return;
    ++tcp_https_trace_events;
    serial_write("NEXUS HTTPS TCP ");
    serial_write(stage);
    if (segment != NULL && length >= NET_TCP_HEADER) {
        const size_t header_length = (size_t)(segment[12] >> 4) * 4U;
        serial_write(" ports=");
        serial_write_hex(read_be16(segment));
        serial_write("/");
        serial_write_hex(read_be16(segment + 2U));
        serial_write(" flags=");
        serial_write_hex(segment[13]);
        serial_write(" seq=");
        serial_write_hex(read_be32(segment + 4U));
        serial_write(" ack=");
        serial_write_hex(read_be32(segment + 8U));
        serial_write(" bytes=");
        serial_write_hex(header_length <= length
            ? length - header_length : 0U);
    } else {
        serial_write(" length=");
        serial_write_hex(length);
    }
    serial_write(" expected-rx=");
    serial_write_hex(tcp_connection.remote_sequence);
    serial_write(" local-seq=");
    serial_write_hex(tcp_connection.local_sequence);
    serial_write(" ack-expected=");
    serial_write_hex(tcp_connection.ack_expected);
    serial_write(" ack-waiting=");
    serial_write_hex(tcp_connection.ack_waiting ? 1U : 0U);
    serial_write("\r\n");
}

static bool tcp_sequence_before(uint32_t left, uint32_t right) {
    return (int32_t)(left - right) < 0;
}

static size_t tcp_receive_space(void) {
    if (tcp_connection.receive_length >= NET_TCP_RX_CAPACITY) return 0U;
    return NET_TCP_RX_CAPACITY - tcp_connection.receive_length;
}

static void tcp_clear_reorder(void) {
    for (size_t i = 0U; i < NET_TCP_REORDER_SLOTS; ++i)
        tcp_connection.reorder[i].valid = false;
    tcp_connection.reorder_length = 0U;
}

/*
 * Queue one future segment inside the advertised receive window. The queue is
 * intentionally small: it covers bursts reordered by a NIC/virtual switch,
 * while retransmission remains the recovery path when the queue is full.
 */
static bool tcp_queue_out_of_order(uint32_t sequence,
                                   const uint8_t *payload,
                                   size_t payload_length, bool fin) {
    if ((payload_length != 0U && payload == NULL) ||
        payload_length > NET_TCP_REORDER_PAYLOAD_MAX ||
        (payload_length == 0U && !fin)) return false;

    const uint32_t forward = sequence - tcp_connection.remote_sequence;
    if (forward >= 0x80000000U) return false;
    const size_t available = tcp_receive_space();
    if ((size_t)forward > available ||
        payload_length > available - (size_t)forward ||
        (fin && (size_t)forward + payload_length >= available)) return false;

    size_t free_slot = NET_TCP_REORDER_SLOTS;
    const size_t new_span = payload_length + (fin ? 1U : 0U);
    for (size_t i = 0U; i < NET_TCP_REORDER_SLOTS; ++i) {
        struct net_tcp_reorder_entry *entry = &tcp_connection.reorder[i];
        if (!entry->valid) {
            if (free_slot == NET_TCP_REORDER_SLOTS) free_slot = i;
            continue;
        }
        if (entry->sequence == sequence) {
            if (entry->payload_length != payload_length || entry->fin != fin)
                return false;
            for (size_t j = 0U; j < payload_length; ++j)
                if (entry->payload[j] != payload[j]) return false;
            return true; /* Exact retransmission of an already queued segment. */
        }

        const uint32_t existing_offset =
            entry->sequence - tcp_connection.remote_sequence;
        const size_t existing_span =
            (size_t)entry->payload_length + (entry->fin ? 1U : 0U);
        const size_t new_start = (size_t)forward;
        const size_t existing_start = (size_t)existing_offset;
        if (new_span != 0U && existing_span != 0U &&
            new_start < existing_start + existing_span &&
            existing_start < new_start + new_span) {
            /* Do not keep conflicting overlapping segments. */
            return false;
        }
    }
    if (free_slot == NET_TCP_REORDER_SLOTS) return false;

    struct net_tcp_reorder_entry *entry =
        &tcp_connection.reorder[free_slot];
    entry->sequence = sequence;
    entry->payload_length = (uint16_t)payload_length;
    entry->fin = fin;
    for (size_t i = 0U; i < payload_length; ++i)
        entry->payload[i] = payload[i];
    entry->valid = true;
    tcp_connection.reorder_length += new_span;
    return true;
}

/* Move any now-contiguous queued segments into the application receive FIFO. */
static void tcp_drain_reorder(void) {
    for (;;) {
        bool progressed = false;
        for (size_t i = 0U; i < NET_TCP_REORDER_SLOTS; ++i) {
            struct net_tcp_reorder_entry *entry = &tcp_connection.reorder[i];
            if (!entry->valid) continue;

            const uint32_t distance = entry->sequence -
                                      tcp_connection.remote_sequence;
            if (distance != 0U) {
                if (distance < 0x80000000U) continue;

                /* The missing segment overlapped this queued retransmission. */
                const uint32_t overlap = tcp_connection.remote_sequence -
                                         entry->sequence;
                const size_t span =
                    (size_t)entry->payload_length + (entry->fin ? 1U : 0U);
                if ((size_t)overlap >= span) {
                    tcp_connection.reorder_length -= span;
                    entry->valid = false;
                    progressed = true;
                    break;
                }
                if ((size_t)overlap < entry->payload_length) {
                    const size_t trim = (size_t)overlap;
                    for (size_t j = trim; j < entry->payload_length; ++j)
                        entry->payload[j - trim] = entry->payload[j];
                    entry->payload_length =
                        (uint16_t)((size_t)entry->payload_length - trim);
                    entry->sequence = tcp_connection.remote_sequence;
                    tcp_connection.reorder_length -= trim;
                } else if ((size_t)overlap == entry->payload_length &&
                           entry->fin) {
                    tcp_connection.reorder_length -= entry->payload_length;
                    entry->payload_length = 0U;
                    entry->sequence = tcp_connection.remote_sequence;
                } else {
                    tcp_connection.reorder_length -= span;
                    entry->valid = false;
                    progressed = true;
                    break;
                }
            }

            if (entry->sequence != tcp_connection.remote_sequence) continue;
            if ((size_t)entry->payload_length > tcp_receive_space()) continue;

            const size_t amount = entry->payload_length;
            if (amount != 0U) {
                copy_bytes(tcp_connection.receive_buffer +
                               tcp_connection.receive_length,
                           entry->payload, amount);
                tcp_connection.receive_length += amount;
                tcp_connection.remote_sequence += (uint32_t)amount;
                tcp_connection.reorder_length -= amount;
            }
            if (entry->fin) {
                ++tcp_connection.remote_sequence;
                tcp_connection.peer_closed = true;
                --tcp_connection.reorder_length;
            }
            entry->valid = false;
            progressed = true;
            break;
        }
        if (!progressed) break;
    }
}

static void receive_tcp(const uint8_t *segment, size_t length,
                        const uint8_t source_ip[4],
                        const uint8_t destination_ip[4]) {
    const bool https_trace = tcp_connection.remote_port == 443U;
    if (https_trace) tcp_https_trace("RX", segment, length);
    if (!tcp_connection.active || segment == NULL || source_ip == NULL ||
        destination_ip == NULL || length < NET_TCP_HEADER ||
        !equal_bytes(source_ip, tcp_connection.remote_ipv4, 4U) ||
        !equal_bytes(destination_ip, network.ipv4, 4U) ||
        read_be16(segment) != tcp_connection.remote_port ||
        read_be16(segment + 2U) != tcp_connection.local_port ||
        tcp_checksum_value(source_ip, destination_ip, segment, length) != 0U) {
        if (https_trace) tcp_https_trace("DROP TUPLE/CHECKSUM", segment, length);
        return;
    }

    const size_t header_length = (size_t)(segment[12] >> 4) * 4U;
    if (header_length < NET_TCP_HEADER || header_length > length) {
        if (https_trace) tcp_https_trace("DROP HEADER", segment, length);
        return;
    }
    const uint8_t flags = segment[13];
    const uint32_t sequence = read_be32(segment + 4U);
    const uint32_t acknowledgement = read_be32(segment + 8U);
    const uint8_t *payload = segment + header_length;
    const size_t payload_length = length - header_length;
    const bool fin = (flags & TCP_FLAG_FIN) != 0U;

    if ((flags & TCP_FLAG_RST) != 0U) {
        if (https_trace) tcp_https_trace("RST", segment, length);
        tcp_connection.failed = true;
        tcp_connection.connected = false;
        return;
    }

    if (tcp_connection.connecting &&
        (flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) ==
            (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
        acknowledgement == tcp_connection.local_sequence + 1U) {
        tcp_connection.remote_sequence = sequence + 1U;
        tcp_connection.local_sequence = acknowledgement;
        tcp_connection.connecting = false;
        tcp_connection.connected = true;
        (void)send_tcp_segment(TCP_FLAG_ACK, NULL, 0U);
        return;
    }

    if (!tcp_connection.connected || (flags & TCP_FLAG_ACK) == 0U) return;

    /*
     * ACK processing is independent from payload sequence order. A valid ACK
     * on an out-of-order data segment must still release our stop-and-wait
     * sender; old duplicate ACKs are harmless, future ACKs are rejected.
     */
    if (tcp_connection.ack_waiting &&
        acknowledgement == tcp_connection.ack_expected) {
        tcp_connection.local_sequence = acknowledgement;
        tcp_connection.ack_received = true;
        tcp_connection.ack_waiting = false;
    } else if (acknowledgement != tcp_connection.local_sequence &&
               !tcp_sequence_before(acknowledgement,
                                    tcp_connection.local_sequence)) {
        if (https_trace) tcp_https_trace("DROP ACK", segment, length);
        return;
    }

    const uint32_t expected = tcp_connection.remote_sequence;
    const int32_t sequence_delta = (int32_t)(sequence - expected);
    if (sequence_delta > 0) {
        if (payload_length != 0U || fin)
            (void)tcp_queue_out_of_order(sequence, payload, payload_length, fin);
        if (https_trace) tcp_https_trace("QUEUE OUT OF ORDER", segment, length);
        (void)send_tcp_segment(TCP_FLAG_ACK, NULL, 0U);
        return;
    }

    size_t trim = 0U;
    if (sequence_delta < 0) {
        trim = (size_t)(expected - sequence);
        const size_t original_span = payload_length + (fin ? 1U : 0U);
        if (trim >= original_span) {
            /* Already-consumed duplicate: repeat the cumulative ACK. */
            if (payload_length != 0U || fin)
                (void)send_tcp_segment(TCP_FLAG_ACK, NULL, 0U);
            return;
        }
    }

    const size_t accepted_length =
        trim < payload_length ? payload_length - trim : 0U;
    const uint8_t *accepted_payload = payload + trim;
    if (accepted_length > tcp_receive_space()) {
        (void)send_tcp_segment(TCP_FLAG_ACK, NULL, 0U);
        return;
    }

    if (accepted_length != 0U) {
        if (https_trace) tcp_https_trace("ACCEPT PAYLOAD", segment, length);
        copy_bytes(tcp_connection.receive_buffer +
                       tcp_connection.receive_length,
                   accepted_payload, accepted_length);
        tcp_connection.receive_length += accepted_length;
        tcp_connection.remote_sequence += (uint32_t)accepted_length;
    }

    const bool fin_is_next = fin &&
        trim <= payload_length &&
        trim + accepted_length == payload_length;
    if (fin_is_next) {
        ++tcp_connection.remote_sequence;
        tcp_connection.peer_closed = true;
    }

    tcp_drain_reorder();
    if (payload_length != 0U || fin)
        (void)send_tcp_segment(TCP_FLAG_ACK, NULL, 0U);
}

bool net_tcp_connect(const uint8_t remote_ipv4[4], uint16_t remote_port,
                     uint64_t timeout_ticks) {
    if (!network.ready || dhcp_state != NET_DHCP_BOUND ||
        remote_ipv4 == NULL || ipv4_zero(remote_ipv4) ||
        remote_port == 0U || timeout_ticks == 0U ||
        tcp_connection.active) return false;

    uint8_t next_hop[4];
    uint8_t neighbor[6];
    if (!ipv4_next_hop(remote_ipv4, next_hop)) return false;

    tcp_connection = (struct net_tcp_connection){0};
    tcp_connection.active = true;
    if (remote_port == 443U) tcp_https_trace_events = 0U;
    copy_bytes(tcp_connection.remote_ipv4, remote_ipv4, 4U);
    tcp_connection.remote_port = remote_port;
    tcp_connection.local_port = tcp_next_port++;
    if (tcp_next_port < 49152U) tcp_next_port = 49152U;

    uint64_t arp_budget = timeout_ticks / 2U;
    if (arp_budget == 0U) arp_budget = 1U;
    const uint64_t arp_start = timer_ticks();
    bool arp_probe_sent = false;
    uint64_t last_arp_tick = 0U;
    while (!arp_lookup(next_hop, neighbor) &&
           timer_ticks() - arp_start < arp_budget) {
        const uint64_t now = timer_ticks();
        if (!arp_probe_sent || now - last_arp_tick >= NET_ARP_RETRY_TICKS) {
            (void)send_arp_request(next_hop);
            last_arp_tick = now;
            arp_probe_sent = true;
        }
        net_poll();
        __asm__ volatile ("pause");
    }
    if (!arp_lookup(next_hop, neighbor)) {
        tcp_connection.active = false;
        return false;
    }

    tcp_connection.local_sequence =
        (uint32_t)(timer_ticks() ^ ((uint64_t)network.ipv4_id << 16) ^
                   0x4E585553U);
    tcp_connection.connecting = true;
    if (!send_tcp_segment(TCP_FLAG_SYN, NULL, 0U)) {
        tcp_connection.active = false;
        return false;
    }
    tcp_connection.syn_last_send_tick = timer_ticks();
    tcp_connection.syn_retry_count = 0U;

    uint64_t connect_budget = timeout_ticks / 2U;
    if (connect_budget == 0U) connect_budget = 1U;
    const uint64_t connect_start = timer_ticks();
    while (!tcp_connection.connected && !tcp_connection.failed &&
           timer_ticks() - connect_start < connect_budget) {
        net_poll();
        __asm__ volatile ("pause");
    }
    if (!tcp_connection.connected) {
        tcp_connection.active = false;
        return false;
    }
    return true;
}

static bool tcp_send_reliable_segment(const uint8_t *payload,
                                      size_t payload_length) {
    if (payload_length == 0U || payload_length > NET_TCP_MAX_PAYLOAD)
        return false;

    tcp_connection.ack_expected =
        tcp_connection.local_sequence + (uint32_t)payload_length;
    tcp_connection.ack_waiting = true;
    tcp_connection.ack_received = false;
    if (!send_tcp_segment(TCP_FLAG_ACK | TCP_FLAG_PSH,
                          payload, payload_length)) {
        tcp_connection.ack_waiting = false;
        return false;
    }

    const uint64_t start_tick = timer_ticks();
    uint64_t last_send_tick = start_tick;
    uint32_t retries = 0U;
    while (!tcp_connection.ack_received && !tcp_connection.failed) {
        net_poll();
        if (tcp_connection.ack_received) return true;

        const uint64_t now = timer_ticks();
        if (now - start_tick >= NET_TCP_DATA_TIMEOUT_TICKS) break;
        if (now - last_send_tick >= NET_TCP_RETRY_TICKS) {
            if (retries >= NET_TCP_MAX_RETRIES) break;
            (void)send_tcp_segment(TCP_FLAG_ACK | TCP_FLAG_PSH,
                                   payload, payload_length);
            ++retries;
            last_send_tick = now;
        }
        __asm__ volatile ("pause");
    }

    tcp_connection.ack_waiting = false;
    tcp_connection.failed = true;
    tcp_connection.connected = false;
    tcp_connection.active = false;
    return false;
}

bool net_tcp_send(const void *payload, uint16_t payload_length) {
    if (!tcp_connection.active || !tcp_connection.connected ||
        tcp_connection.peer_closed ||
        (payload_length != 0U && payload == NULL)) return false;
    if (payload_length == 0U) return true;

    const uint8_t *bytes = (const uint8_t *)payload;
    size_t offset = 0U;
    while (offset < payload_length) {
        size_t amount = (size_t)payload_length - offset;
        if (amount > NET_TCP_MAX_PAYLOAD) amount = NET_TCP_MAX_PAYLOAD;
        if (!tcp_send_reliable_segment(bytes + offset, amount)) return false;
        offset += amount;
    }
    return true;
}

static void tcp_retry_pending_syn(void) {
    if (!tcp_connection.active || !tcp_connection.connecting) return;
    const uint64_t now = timer_ticks();
    if (now - tcp_connection.syn_last_send_tick < NET_TCP_RETRY_TICKS)
        return;
    if (tcp_connection.syn_retry_count >= NET_TCP_MAX_RETRIES) {
        tcp_connection.failed = true;
        tcp_connection.connecting = false;
        return;
    }
    (void)send_tcp_segment(TCP_FLAG_SYN, NULL, 0U);
    ++tcp_connection.syn_retry_count;
    tcp_connection.syn_last_send_tick = now;
}

size_t net_tcp_receive(void *buffer, size_t capacity) {
    if (!tcp_connection.active || buffer == NULL || capacity == 0U ||
        tcp_connection.receive_length == 0U) return 0U;
    size_t count = tcp_connection.receive_length;
    if (count > capacity) count = capacity;
    copy_bytes(buffer, tcp_connection.receive_buffer, count);
    tcp_connection.receive_length -= count;
    for (size_t i = 0; i < tcp_connection.receive_length; ++i)
        tcp_connection.receive_buffer[i] =
            tcp_connection.receive_buffer[count + i];
    return count;
}

enum nexus_tcp_receive_result net_tcp_receive_wait(
    void *buffer, size_t capacity, uint64_t timeout_ticks, size_t *received) {
    if (received == NULL) return NEXUS_TCP_RECEIVE_ERROR;
    *received = 0U;
    if (!tcp_connection.active || !tcp_connection.connected ||
        buffer == NULL || capacity == 0U || timeout_ticks == 0U)
        return NEXUS_TCP_RECEIVE_ERROR;

    const uint64_t start = timer_ticks();
    while (tcp_connection.receive_length == 0U &&
           !tcp_connection.peer_closed && !tcp_connection.failed &&
           tcp_connection.active && tcp_connection.connected) {
        if (timer_ticks() - start >= timeout_ticks)
            return NEXUS_TCP_RECEIVE_TIMEOUT;
        net_poll();
        __asm__ volatile ("pause");
    }

    if (tcp_connection.receive_length != 0U) {
        *received = net_tcp_receive(buffer, capacity);
        return *received != 0U ? NEXUS_TCP_RECEIVE_DATA
                               : NEXUS_TCP_RECEIVE_ERROR;
    }
    if (tcp_connection.peer_closed) return NEXUS_TCP_RECEIVE_EOF;
    return NEXUS_TCP_RECEIVE_ERROR;
}

void net_tcp_close(void) {
    if (!tcp_connection.active) return;
    if (tcp_connection.connected && !tcp_connection.peer_closed &&
        send_tcp_segment(TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0U)) {
        ++tcp_connection.local_sequence;
    }
    tcp_connection.active = false;
    tcp_connection.connected = false;
    tcp_connection.connecting = false;
}
