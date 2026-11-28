#ifndef NEXUS_BLUETOOTH_UART_HOST_H
#define NEXUS_BLUETOOTH_UART_HOST_H

#include "bluetooth_hci.h"
#include "bluetooth_uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_BT_UART_HOST_MAX_CONNECTIONS 4U
#define NEXUS_BT_UART_HOST_PDU_QUEUE_DEPTH 4U

struct nexus_bt_uart_host_connection {
    bool active;
    struct nexus_bt_hci_connection link;
    struct nexus_bt_acl_reassembler reassembler;
};

struct nexus_bt_uart_host_pdu {
    uint16_t connection_handle;
    uint16_t length;
    uint8_t bytes[NEXUS_BT_ACL_MAX_L2CAP_PDU];
};

/* UART transport + serialized HCI reset/version/address bring-up. The caller
 * supplies the UART I/O base, input clock and baud rate (for example, from a
 * board description or platform configuration); no port is guessed. */
struct nexus_bt_uart_host {
    bool initialized;
    struct nexus_bt_uart uart;
    struct nexus_bt_hci_host hci;
    struct nexus_bt_uart_host_connection connections[
        NEXUS_BT_UART_HOST_MAX_CONNECTIONS];
    struct nexus_bt_uart_host_pdu receive_queue[
        NEXUS_BT_UART_HOST_PDU_QUEUE_DEPTH];
    uint8_t queue_head;
    uint8_t queue_tail;
    uint8_t receive_packet[NEXUS_BT_UART_MAX_PACKET];
    uint32_t dropped_packets;
};

bool nexus_bt_uart_host_init(struct nexus_bt_uart_host *host,
    uint16_t io_base, uint32_t input_clock_hz, uint32_t baud_rate,
    bool hardware_flow_control, uint32_t transmit_spin_limit);

/* Poll one complete H4 packet (up to byte_budget incoming bytes), process HCI
 * bring-up/link events or ACL/L2CAP data, and submit the next credited HCI
 * controller command. Returns true when a complete packet was consumed. */
bool nexus_bt_uart_host_poll(struct nexus_bt_uart_host *host,
    size_t byte_budget, uint32_t transmit_spin_limit);

/* Send one HCI ACL packet for a known connection. payload is an ACL fragment;
 * packet_boundary is 0/2 for a start or 1 for a continuation. */
bool nexus_bt_uart_host_send_acl(struct nexus_bt_uart_host *host,
    uint16_t connection_handle, uint8_t packet_boundary,
    const uint8_t *payload, size_t payload_length,
    uint32_t transmit_spin_limit);

/* Read a fully reassembled L2CAP PDU. A short destination reports the required
 * length and keeps the queue item available for retry. */
bool nexus_bt_uart_host_read_l2cap(struct nexus_bt_uart_host *host,
    uint16_t *connection_handle, uint8_t *pdu, size_t pdu_capacity,
    size_t *pdu_length);

#endif
