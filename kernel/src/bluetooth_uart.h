#ifndef NEXUS_BLUETOOTH_UART_H
#define NEXUS_BLUETOOTH_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_BT_UART_MAX_ACL_PAYLOAD 1024U
#define NEXUS_BT_UART_MAX_PACKET (5U + NEXUS_BT_UART_MAX_ACL_PAYLOAD)

/* Polled 16550-compatible UART carrying the HCI H4 packet format. This can
 * drive a discrete UART HCI controller; it is not a USB HCI transport. */
struct nexus_bt_uart {
    uint16_t io_base;
    bool initialized;
    bool hardware_flow_control;
    bool rx_complete;
    uint16_t rx_length;
    uint16_t rx_expected;
    uint32_t rx_dropped_packets;
    uint8_t rx_packet[NEXUS_BT_UART_MAX_PACKET];
};

bool nexus_bt_uart_init(struct nexus_bt_uart *uart, uint16_t io_base,
    uint32_t input_clock_hz, uint32_t baud_rate, bool hardware_flow_control);

/* packet must contain a complete H4 Command (type 0x01) or ACL (type 0x02)
 * packet. Polling is bounded by spin_limit for every transmitted byte. */
bool nexus_bt_uart_send(struct nexus_bt_uart *uart,
    const uint8_t *packet, size_t packet_length, uint32_t spin_limit);

/* Consume up to byte_budget currently available UART bytes. A fragmented H4
 * packet remains buffered across calls. The destination must fit the complete
 * packet; on a too-small destination, *packet_length reports the needed size
 * and the completed packet remains queued for a later call. */
bool nexus_bt_uart_poll_receive(struct nexus_bt_uart *uart,
    uint8_t *packet, size_t packet_capacity, size_t *packet_length,
    size_t byte_budget);

#endif
