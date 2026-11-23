#ifndef NEXUS_NET_H
#define NEXUS_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct nexus_net_info {
    bool interface_ready;
    bool link_up;
    bool ipv4_configured;
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
bool net_udp_send(const uint8_t destination_ipv4[4],
                  uint16_t source_port, uint16_t destination_port,
                  const void *payload, uint16_t payload_length);
bool net_tcp_connect(const uint8_t remote_ipv4[4], uint16_t remote_port,
                     uint64_t timeout_ticks);
bool net_tcp_send(const void *payload, uint16_t payload_length);
size_t net_tcp_receive(void *buffer, size_t capacity);
void net_tcp_close(void);

#endif
