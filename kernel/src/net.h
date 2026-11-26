#ifndef NEXUS_NET_H
#define NEXUS_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "netdev.h"

enum nexus_dhcp_client_state {
    NEXUS_DHCP_STATE_IDLE = 0,
    NEXUS_DHCP_STATE_DISCOVERING,
    NEXUS_DHCP_STATE_REQUESTING,
    NEXUS_DHCP_STATE_BOUND,
    NEXUS_DHCP_STATE_RENEWING,
    NEXUS_DHCP_STATE_REBINDING
};

enum nexus_tcp_receive_result {
    NEXUS_TCP_RECEIVE_ERROR = -1,
    NEXUS_TCP_RECEIVE_TIMEOUT = 0,
    NEXUS_TCP_RECEIVE_DATA = 1,
    NEXUS_TCP_RECEIVE_EOF = 2
};

struct nexus_net_info {
    char interface_name[16];
    enum nexus_netdev_medium interface_medium;
    bool interface_ready;
    bool link_up;
    bool ipv4_configured;
    enum nexus_dhcp_client_state dhcp_state;
    bool dhcp_lease_valid;
    uint32_t dhcp_lease_seconds;
    uint32_t dhcp_t1_seconds;
    uint32_t dhcp_t2_seconds;
    uint32_t dhcp_lease_remaining_seconds;
    uint32_t dhcp_renew_in_seconds;
    uint8_t mac[6];
    uint8_t ipv4[4];
    uint8_t subnet_mask[4];
    uint8_t gateway[4];
    uint8_t dns_server[4];
};

bool net_init(void);
void net_poll(void);
bool net_get_info(struct nexus_net_info *info);
bool net_dns_resolve(const char *hostname, uint8_t result[4],
                     uint64_t timeout_ticks);
/* Send a bounded IPv4 ICMP echo probe. reply_ttl may be NULL. */
bool net_ping(const uint8_t destination_ipv4[4], uint64_t timeout_ticks,
              uint8_t *reply_ttl);
bool net_udp_send(const uint8_t destination_ipv4[4],
                  uint16_t source_port, uint16_t destination_port,
                  const void *payload, uint16_t payload_length);
bool net_tcp_connect(const uint8_t remote_ipv4[4], uint16_t remote_port,
                     uint64_t timeout_ticks);
/*
 * Send a complete caller-owned buffer using bounded MSS-sized segments.
 * This bootstrap client waits for each cumulative ACK and retries a segment
 * a fixed number of times. It is synchronous and stop-and-wait; it does not
 * yet implement congestion control or out-of-order/partial ACK handling.
 */
bool net_tcp_send(const void *payload, uint16_t payload_length);
size_t net_tcp_receive(void *buffer, size_t capacity);
/*
 * Wait for data, peer EOF, transport failure, or a bounded timeout.
 * On DATA, *received is non-zero. On EOF or TIMEOUT it is zero.
 */
enum nexus_tcp_receive_result net_tcp_receive_wait(
    void *buffer, size_t capacity, uint64_t timeout_ticks, size_t *received);
void net_tcp_close(void);

#endif
