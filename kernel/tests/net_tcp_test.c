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

uint64_t timer_ticks(void) {
    return ++fake_ticks;
}

void serial_write(const char *message) {
    (void)message;
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
    if (ip[9] != IP_PROTOCOL_TCP || length < 54U) return true;
    const size_t ip_header = (size_t)(ip[0] & 0x0FU) * 4U;
    if (ip_header < NET_IPV4_HEADER ||
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
    return 0;
}
