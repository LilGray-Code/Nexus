#include "../src/net.c"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static uint64_t fake_ticks;
static uint8_t queued_frame[NET_MAX_FRAME];
static size_t queued_frame_length;
static bool queued_frame_ready;
static uint8_t remote_mac[6] = {0x02U,0xAAU,0xBBU,0xCCU,0xDDU,0xEEU};
static const uint8_t remote_ip[4] = {192U,0U,2U,20U};
static unsigned syn_transmissions;
static unsigned data_transmissions;
static uint32_t last_data_sequence;
static size_t attempts_for_sequence;
static bool have_data_sequence;
static bool drop_all_data_acks;
static bool inject_bad_ack_once;
static bool bad_ack_injected;
static uint8_t last_data_payload[NET_TCP_MAX_PAYLOAD];
static size_t last_data_payload_length;
static unsigned dns_request_count;
static unsigned dns_reply_on_request;

static void queue_dns_test_udp_reply(uint16_t transaction_id);

uint64_t timer_ticks(void) {
    return ++fake_ticks;
}

void serial_write(const char *message) {
    (void)message;
}

void serial_write_hex(uint64_t value) {
    (void)value;
}

bool netdev_link_up(void) {
    return true;
}

bool netdev_get_info(struct nexus_netdev_info *info) {
    if (info == NULL) return false;
    *info = (struct nexus_netdev_info){0};
    for (size_t i = 0U; i < sizeof(network.mac); ++i)
        info->mac[i] = network.mac[i];
    for (size_t i = 0U; i < sizeof(network.interface_name); ++i)
        info->name[i] = network.interface_name[i];
    info->medium = network.interface_medium;
    info->max_frame_size = NET_MAX_FRAME;
    info->link_up = true;
    return true;
}

bool netdev_receive_frame(void *buffer, size_t capacity, size_t *length) {
    if (buffer == NULL || length == NULL || !queued_frame_ready ||
        queued_frame_length > capacity) return false;
    uint8_t *out = (uint8_t *)buffer;
    for (size_t i = 0U; i < queued_frame_length; ++i)
        out[i] = queued_frame[i];
    *length = queued_frame_length;
    queued_frame_ready = false;
    queued_frame_length = 0U;
    return true;
}

static void queue_tcp_response(uint8_t flags, uint32_t sequence,
                               uint32_t acknowledgement) {
    assert(!queued_frame_ready);
    uint8_t frame[NET_MAX_FRAME] = {0};
    copy_bytes(frame, network.mac, 6U);
    copy_bytes(frame + 6U, remote_mac, 6U);
    write_be16(frame + 12U, ETHERTYPE_IPV4);

    uint8_t *ip = frame + NET_ETHERNET_HEADER;
    ip[0] = 0x45U;
    write_be16(ip + 2U, NET_IPV4_HEADER + NET_TCP_HEADER);
    write_be16(ip + 4U, 1U);
    write_be16(ip + 6U, 0U);
    ip[8] = 64U;
    ip[9] = IP_PROTOCOL_TCP;
    write_be16(ip + 10U, 0U);
    copy_bytes(ip + 12U, remote_ip, 4U);
    copy_bytes(ip + 16U, network.ipv4, 4U);
    write_be16(ip + 10U, checksum(ip, NET_IPV4_HEADER));

    uint8_t *tcp = ip + NET_IPV4_HEADER;
    write_be16(tcp, 443U);
    write_be16(tcp + 2U, tcp_connection.local_port);
    write_be32(tcp + 4U, sequence);
    write_be32(tcp + 8U, acknowledgement);
    tcp[12] = 0x50U;
    tcp[13] = flags;
    write_be16(tcp + 14U, (uint16_t)NET_TCP_RX_CAPACITY);
    write_be16(tcp + 16U, 0U);
    write_be16(tcp + 18U, 0U);
    write_be16(tcp + 16U,
               tcp_checksum_value(remote_ip, network.ipv4, tcp,
                                  NET_TCP_HEADER));

    queued_frame_length = NET_ETHERNET_HEADER +
                          NET_IPV4_HEADER + NET_TCP_HEADER;
    for (size_t i = 0U; i < queued_frame_length; ++i)
        queued_frame[i] = frame[i];
    queued_frame_ready = true;
}

bool netdev_send_frame(const void *frame_data, size_t length) {
    if (frame_data == NULL || length < NET_ETHERNET_HEADER) return false;
    const uint8_t *frame = (const uint8_t *)frame_data;
    if (read_be16(frame + 12U) != ETHERTYPE_IPV4 ||
        length < NET_ETHERNET_HEADER + NET_IPV4_HEADER) return true;
    const uint8_t *ip = frame + NET_ETHERNET_HEADER;
    const size_t ip_header = (size_t)(ip[0] & 0x0FU) * 4U;
    if (ip_header < NET_IPV4_HEADER ||
        length < NET_ETHERNET_HEADER + ip_header) return true;
    if (ip[9] == IP_PROTOCOL_UDP) {
        if (length < NET_ETHERNET_HEADER + ip_header + NET_UDP_HEADER)
            return true;
        const uint8_t *udp = ip + ip_header;
        if (read_be16(udp) == NET_DNS_CLIENT_PORT &&
            read_be16(udp + 2U) == NET_DNS_SERVER_PORT) {
            ++dns_request_count;
            /* Simulate a dropped first request and answer a later retry. */
            if (dns_reply_on_request != 0U &&
                dns_request_count >= dns_reply_on_request) {
                queue_dns_test_udp_reply(read_be16(udp + NET_UDP_HEADER));
            }
        }
        return true;
    }
    if (ip[9] != IP_PROTOCOL_TCP ||
        length < NET_ETHERNET_HEADER + ip_header + NET_TCP_HEADER) return true;
    const uint8_t *tcp = ip + ip_header;
    const uint8_t flags = tcp[13];
    const uint32_t sequence = read_be32(tcp + 4U);

    if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == TCP_FLAG_SYN) {
        ++syn_transmissions;
        /* Drop the first SYN; answer only the retransmission. */
        if (syn_transmissions == 2U)
            queue_tcp_response(TCP_FLAG_SYN | TCP_FLAG_ACK,
                               0x13572468U, sequence + 1U);
    } else if ((flags & TCP_FLAG_PSH) != 0U) {
        ++data_transmissions;
        const size_t tcp_header = (size_t)(tcp[12] >> 4) * 4U;
        const size_t payload_length =
            length - NET_ETHERNET_HEADER - ip_header - tcp_header;
        if (!have_data_sequence || sequence != last_data_sequence) {
            have_data_sequence = true;
            last_data_sequence = sequence;
            attempts_for_sequence = 1U;
            last_data_payload_length = payload_length;
            assert(payload_length <= sizeof(last_data_payload));
            for (size_t i = 0U; i < payload_length; ++i)
                last_data_payload[i] = tcp[tcp_header + i];
        } else {
            ++attempts_for_sequence;
            assert(payload_length == last_data_payload_length);
            for (size_t i = 0U; i < payload_length; ++i)
                assert(tcp[tcp_header + i] == last_data_payload[i]);
        }

        if (!drop_all_data_acks && attempts_for_sequence >= 2U) {
            if (inject_bad_ack_once && !bad_ack_injected) {
                /* Future peer sequence: must not acknowledge the send. */
                queue_tcp_response(TCP_FLAG_ACK,
                                   tcp_connection.remote_sequence + 1U,
                                   sequence + (uint32_t)payload_length);
                bad_ack_injected = true;
            } else {
                queue_tcp_response(TCP_FLAG_ACK, tcp_connection.remote_sequence,
                                   sequence + (uint32_t)payload_length);
            }
        }
    }
    return true;
}

static void setup_network(void) {
    fake_ticks = 0U;
    queued_frame_length = 0U;
    queued_frame_ready = false;
    syn_transmissions = 0U;
    data_transmissions = 0U;
    last_data_sequence = 0U;
    attempts_for_sequence = 0U;
    have_data_sequence = false;
    drop_all_data_acks = false;
    inject_bad_ack_once = true;
    bad_ack_injected = false;
    last_data_payload_length = 0U;
    dns_request_count = 0U;
    dns_reply_on_request = 0U;

    network = (struct net_state){0};
    network.ready = true;
    network.link_up = true;
    network.interface_medium = NEXUS_NETDEV_MEDIUM_ETHERNET;
    const uint8_t local_mac[6] = {0x02U,0x10U,0x20U,0x30U,0x40U,0x50U};
    copy_bytes(network.mac, local_mac, sizeof(local_mac));
    const char name[] = "test0";
    copy_bytes(network.interface_name, name, sizeof(name));
    const uint8_t local_ip[4] = {192U,0U,2U,10U};
    copy_bytes(network.ipv4, local_ip, sizeof(local_ip));
    subnet_mask[0] = 255U;
    subnet_mask[1] = 255U;
    subnet_mask[2] = 255U;
    subnet_mask[3] = 0U;
    dhcp_state = NET_DHCP_BOUND;

    network.arp[0].valid = true;
    copy_bytes(network.arp[0].ipv4, remote_ip, sizeof(remote_ip));
    copy_bytes(network.arp[0].mac, remote_mac, sizeof(remote_mac));
    tcp_connection = (struct net_tcp_connection){0};
    dns_query_active = false;
    dns_query_done = false;
    dns_query_success = false;
    dns_query_id = 0U;
    dns_query_ttl = 0U;
    dns_query_name[0] = '\0';
    for (size_t i = 0U; i < 4U; ++i) dns_ipv4[i] = 0U;
    dns_cache_clear();
}

static size_t dns_test_append_name(uint8_t *packet, size_t offset,
                                   const char *hostname) {
    size_t input = 0U;
    while (hostname[input] != '\0') {
        size_t label_length = 0U;
        while (hostname[input + label_length] != '\0' &&
               hostname[input + label_length] != '.') ++label_length;
        assert(label_length != 0U && label_length <= 63U);
        packet[offset++] = (uint8_t)label_length;
        for (size_t i = 0U; i < label_length; ++i)
            packet[offset++] = (uint8_t)hostname[input + i];
        input += label_length;
        if (hostname[input] == '.') ++input;
    }
    packet[offset++] = 0U;
    return offset;
}

static void queue_dns_test_udp_reply(uint16_t transaction_id) {
    uint8_t dns[NET_DNS_PACKET_SIZE] = {0};
    write_be16(dns, transaction_id);
    write_be16(dns + 2U, 0x8180U);
    write_be16(dns + 4U, 1U);
    write_be16(dns + 6U, 1U);
    size_t dns_length = dns_test_append_name(
        dns, 12U, dns_query_name);
    write_be16(dns + dns_length, 1U); dns_length += 2U;
    write_be16(dns + dns_length, 1U); dns_length += 2U;

    dns[dns_length++] = 0xC0U;
    dns[dns_length++] = 0x0CU;
    write_be16(dns + dns_length, 1U); dns_length += 2U;
    write_be16(dns + dns_length, 1U); dns_length += 2U;
    write_be32(dns + dns_length, 60U); dns_length += 4U;
    write_be16(dns + dns_length, 4U); dns_length += 2U;
    dns[dns_length++] = 203U;
    dns[dns_length++] = 0U;
    dns[dns_length++] = 113U;
    dns[dns_length++] = 42U;

    assert(!queued_frame_ready);
    const size_t udp_length = NET_UDP_HEADER + dns_length;
    const size_t ip_length = NET_IPV4_HEADER + udp_length;
    queued_frame_length = NET_ETHERNET_HEADER + ip_length;
    assert(queued_frame_length <= sizeof(queued_frame));
    uint8_t *frame = queued_frame;
    copy_bytes(frame, network.mac, 6U);
    copy_bytes(frame + 6U, remote_mac, 6U);
    write_be16(frame + 12U, ETHERTYPE_IPV4);

    uint8_t *ip = frame + NET_ETHERNET_HEADER;
    ip[0] = 0x45U;
    ip[1] = 0U;
    write_be16(ip + 2U, (uint16_t)ip_length);
    write_be16(ip + 4U, 1U);
    write_be16(ip + 6U, 0U);
    ip[8] = 64U;
    ip[9] = IP_PROTOCOL_UDP;
    write_be16(ip + 10U, 0U);
    copy_bytes(ip + 12U, remote_ip, 4U);
    copy_bytes(ip + 16U, network.ipv4, 4U);
    write_be16(ip + 10U, checksum(ip, NET_IPV4_HEADER));

    uint8_t *udp = ip + NET_IPV4_HEADER;
    write_be16(udp, NET_DNS_SERVER_PORT);
    write_be16(udp + 2U, NET_DNS_CLIENT_PORT);
    write_be16(udp + 4U, (uint16_t)udp_length);
    write_be16(udp + 6U, 0U);
    copy_bytes(udp + NET_UDP_HEADER, dns, dns_length);
    queued_frame_ready = true;
}

static size_t dns_test_build_cname_reply(uint8_t *packet,
                                         const char *question_name) {
    for (size_t i = 0U; i < NET_DNS_PACKET_SIZE; ++i) packet[i] = 0U;
    write_be16(packet, 0x4455U);
    write_be16(packet + 2U, 0x8180U);
    write_be16(packet + 4U, 1U);
    write_be16(packet + 6U, 3U);
    size_t cursor = dns_test_append_name(packet, 12U, question_name);
    write_be16(packet + cursor, 1U); cursor += 2U;
    write_be16(packet + cursor, 1U); cursor += 2U;

    /* www.example.com -> edge.example.net */
    packet[cursor++] = 0xC0U;
    packet[cursor++] = 0x0CU;
    write_be16(packet + cursor, 5U); cursor += 2U;
    write_be16(packet + cursor, 1U); cursor += 2U;
    write_be32(packet + cursor, 120U); cursor += 4U;
    const size_t first_length_offset = cursor;
    cursor += 2U;
    const size_t first_name_start = cursor;
    cursor = dns_test_append_name(packet, cursor, "edge.example.net");
    write_be16(packet + first_length_offset,
               (uint16_t)(cursor - first_name_start));

    /* edge.example.net -> origin.example.net */
    cursor = dns_test_append_name(packet, cursor, "edge.example.net");
    write_be16(packet + cursor, 5U); cursor += 2U;
    write_be16(packet + cursor, 1U); cursor += 2U;
    write_be32(packet + cursor, 90U); cursor += 4U;
    const size_t second_length_offset = cursor;
    cursor += 2U;
    const size_t second_name_start = cursor;
    cursor = dns_test_append_name(packet, cursor, "origin.example.net");
    write_be16(packet + second_length_offset,
               (uint16_t)(cursor - second_name_start));

    /* origin.example.net -> 203.0.113.42 */
    cursor = dns_test_append_name(packet, cursor, "origin.example.net");
    write_be16(packet + cursor, 1U); cursor += 2U;
    write_be16(packet + cursor, 1U); cursor += 2U;
    write_be32(packet + cursor, 60U); cursor += 4U;
    write_be16(packet + cursor, 4U); cursor += 2U;
    packet[cursor++] = 203U;
    packet[cursor++] = 0U;
    packet[cursor++] = 113U;
    packet[cursor++] = 42U;
    return cursor;
}

static size_t dns_test_build_bad_pointer_reply(uint8_t *packet) {
    for (size_t i = 0U; i < NET_DNS_PACKET_SIZE; ++i) packet[i] = 0U;
    write_be16(packet, 0x4455U);
    write_be16(packet + 2U, 0x8180U);
    write_be16(packet + 4U, 1U);
    write_be16(packet + 6U, 1U);
    size_t cursor = dns_test_append_name(packet, 12U, "www.example.com");
    write_be16(packet + cursor, 1U); cursor += 2U;
    write_be16(packet + cursor, 1U); cursor += 2U;
    packet[cursor++] = 0xC0U;
    packet[cursor++] = 0xFFU; /* Forward/out-of-packet compression pointer. */
    write_be16(packet + cursor, 1U); cursor += 2U;
    write_be16(packet + cursor, 1U); cursor += 2U;
    write_be32(packet + cursor, 60U); cursor += 4U;
    write_be16(packet + cursor, 4U); cursor += 2U;
    packet[cursor++] = 203U;
    packet[cursor++] = 0U;
    packet[cursor++] = 113U;
    packet[cursor++] = 42U;
    return cursor;
}

static void test_dns_question_validation_and_cname_chains(void) {
    setup_network();
    dns_ipv4[0] = 192U;
    dns_ipv4[1] = 0U;
    dns_ipv4[2] = 2U;
    dns_ipv4[3] = 53U;
    dns_query_active = true;
    dns_query_id = 0x4455U;
    const char expected[] = "www.example.com";
    for (size_t i = 0U; i < sizeof(expected); ++i)
        dns_query_name[i] = expected[i];

    uint8_t reply[NET_DNS_PACKET_SIZE];
    size_t length = dns_test_build_cname_reply(reply, "other.example.com");
    receive_dns(reply, length, dns_ipv4);
    assert(!dns_query_done); /* A matching ID is not enough: question must match. */

    const uint8_t wrong_source[4] = {192U, 0U, 2U, 54U};
    length = dns_test_build_cname_reply(reply, "www.example.com");
    receive_dns(reply, length, wrong_source);
    assert(!dns_query_done);

    receive_dns(reply, length, dns_ipv4);
    assert(dns_query_done && dns_query_success);
    assert(dns_query_ttl == 60U);
    assert(dns_query_result[0] == 203U && dns_query_result[1] == 0U &&
           dns_query_result[2] == 113U && dns_query_result[3] == 42U);

    dns_query_done = false;
    dns_query_success = false;
    length = dns_test_build_bad_pointer_reply(reply);
    receive_dns(reply, length, dns_ipv4);
    assert(dns_query_done && !dns_query_success);
}

static void test_dns_retransmission_and_cache_hit(void) {
    setup_network();
    copy_bytes(dns_ipv4, remote_ip, sizeof(remote_ip));
    dns_reply_on_request = 2U; /* Ignore the first datagram; answer the retry. */

    uint8_t result[4] = {0U, 0U, 0U, 0U};
    assert(net_dns_resolve("www.example.com", result, 1000U));
    assert(dns_request_count == 2U);
    assert(result[0] == 203U && result[1] == 0U &&
           result[2] == 113U && result[3] == 42U);

    uint8_t cached[4] = {0U, 0U, 0U, 0U};
    assert(net_dns_resolve("WWW.EXAMPLE.COM.", cached, 1000U));
    assert(equal_bytes(cached, result, sizeof(result)));
    assert(dns_request_count == 2U); /* Case/trailing-dot cache hit. */

    setup_network();
    copy_bytes(dns_ipv4, remote_ip, sizeof(remote_ip));
    /* No server reply: initial send plus exactly two bounded retries. */
    assert(!net_dns_resolve("no-answer.example", result, 350U));
    assert(dns_request_count == 3U);
    assert(!dns_query_active);
}

static void test_dns_cache_ttl_and_link_reset(void) {
    setup_network();
    const uint8_t address[4] = {203U, 0U, 113U, 42U};
    uint8_t result[4] = {0U, 0U, 0U, 0U};

    fake_ticks = 100U;
    char normalized[NET_DNS_HOSTNAME_MAX + 1U];
    assert(dns_normalize_hostname("WWW.Example.COM.", normalized));
    assert(dns_name_equal(normalized, "www.example.com"));
    dns_cache_store(normalized, address, 60U);
    assert(dns_cache_lookup("www.example.com", result));
    assert(equal_bytes(result, address, sizeof(address)));
    assert(!dns_cache_lookup("other.example.com", result));

    fake_ticks = 6099U;
    assert(dns_cache_lookup("www.example.com", result));
    assert(!dns_cache_lookup("www.example.com", result));

    dns_cache_store("zero-ttl.example", address, 0U);
    assert(!dns_cache_lookup("zero-ttl.example", result));

    dns_cache_store("capped.example", address, 3600U);
    size_t capped_slot = NET_DNS_CACHE_SIZE;
    for (size_t i = 0U; i < NET_DNS_CACHE_SIZE; ++i) {
        if (dns_cache[i].valid &&
            dns_name_equal(dns_cache[i].hostname, "capped.example")) {
            capped_slot = i;
            break;
        }
    }
    assert(capped_slot != NET_DNS_CACHE_SIZE);
    assert(dns_cache[capped_slot].expires_at - timer_ticks() <=
           (uint64_t)NET_DNS_MAX_TTL_SECONDS * NET_DNS_TICKS_PER_SECOND);

    network.link_up = true;
    network_handle_link_change(false);
    assert(!dns_cache_lookup("capped.example", result));
}

static void test_receive_wait_reports_timeout_data_and_eof(void) {
    setup_network();
    assert(net_tcp_connect(remote_ip, 443U, 300U));

    uint8_t output[8] = {0U};
    size_t received = 99U;
    assert(net_tcp_receive_wait(output, sizeof(output), 5U, &received) ==
           NEXUS_TCP_RECEIVE_TIMEOUT);
    assert(received == 0U);

    tcp_connection.receive_buffer[0] = 'T';
    tcp_connection.receive_buffer[1] = 'L';
    tcp_connection.receive_buffer[2] = 'S';
    tcp_connection.receive_length = 3U;
    received = 0U;
    assert(net_tcp_receive_wait(output, sizeof(output), 5U, &received) ==
           NEXUS_TCP_RECEIVE_DATA);
    assert(received == 3U);
    assert(output[0] == 'T' && output[1] == 'L' && output[2] == 'S');
    assert(tcp_connection.receive_length == 0U);

    tcp_connection.peer_closed = true;
    received = 99U;
    assert(net_tcp_receive_wait(output, sizeof(output), 5U, &received) ==
           NEXUS_TCP_RECEIVE_EOF);
    assert(received == 0U);

    tcp_connection.peer_closed = false;
    tcp_connection.failed = true;
    assert(net_tcp_receive_wait(output, sizeof(output), 5U, &received) ==
           NEXUS_TCP_RECEIVE_ERROR);
    assert(net_tcp_receive_wait(output, sizeof(output), 0U, &received) ==
           NEXUS_TCP_RECEIVE_ERROR);
    assert(net_tcp_receive_wait(NULL, sizeof(output), 5U, &received) ==
           NEXUS_TCP_RECEIVE_ERROR);
}

int main(void) {
    setup_network();
    assert(net_tcp_connect(remote_ip, 443U, 300U));
    assert(tcp_connection.connected);
    assert(syn_transmissions == 2U);

    const uint32_t first_data_sequence = tcp_connection.local_sequence;
    uint8_t payload[1300];
    for (size_t i = 0U; i < sizeof(payload); ++i)
        payload[i] = (uint8_t)(i & 0xFFU);

    assert(net_tcp_send(payload, (uint16_t)sizeof(payload)));
    assert(data_transmissions == 5U); /* one bad ACK is ignored; both chunks recover */
    assert(bad_ack_injected);
    assert(tcp_connection.local_sequence ==
           first_data_sequence + (uint32_t)sizeof(payload));
    assert(!tcp_connection.ack_waiting);

    drop_all_data_acks = true;
    const unsigned before_timeout = data_transmissions;
    const uint8_t byte = 0x5AU;
    assert(!net_tcp_send(&byte, 1U));
    assert(data_transmissions - before_timeout == NET_TCP_MAX_RETRIES + 1U);
    assert(!tcp_connection.active && !tcp_connection.connected);
    test_receive_wait_reports_timeout_data_and_eof();
    test_dns_question_validation_and_cname_chains();
    test_dns_cache_ttl_and_link_reset();
    test_dns_retransmission_and_cache_hit();
    return 0;
}
