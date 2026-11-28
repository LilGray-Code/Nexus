#include "bluetooth_uart.h"

#include "bluetooth_hci.h"

#ifdef NEXUS_BT_UART_HOST_TEST
uint8_t nexus_bt_uart_test_inb(uint16_t port);
void nexus_bt_uart_test_outb(uint16_t port, uint8_t value);
#define uart_inb(port) nexus_bt_uart_test_inb(port)
#define uart_outb(port, value) nexus_bt_uart_test_outb((port), (value))
#else
static inline uint8_t uart_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void uart_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
#endif

#define UART_REG_DATA 0U
#define UART_REG_IER  1U
#define UART_REG_FCR  2U
#define UART_REG_LCR  3U
#define UART_REG_MCR  4U
#define UART_REG_LSR  5U
#define UART_REG_MSR  6U

#define UART_LSR_DATA_READY (1U << 0)
#define UART_LSR_THR_EMPTY  (1U << 5)
#define UART_MSR_CTS        (1U << 4)
#define UART_LCR_DLAB       (1U << 7)

static uint16_t uart_port(const struct nexus_bt_uart *uart, uint8_t reg) {
    return (uint16_t)(uart->io_base + reg);
}

bool nexus_bt_uart_init(struct nexus_bt_uart *uart, uint16_t io_base,
    uint32_t input_clock_hz, uint32_t baud_rate, bool hardware_flow_control) {
    if (uart == NULL || io_base == 0U || io_base > 0xFFF9U ||
        input_clock_hz == 0U || baud_rate == 0U) return false;

    const uint64_t denominator = (uint64_t)baud_rate * 16U;
    const uint64_t divisor =
        ((uint64_t)input_clock_hz + denominator / 2U) / denominator;
    if (divisor == 0U || divisor > UINT16_MAX) return false;

    *uart = (struct nexus_bt_uart){0};
    uart->io_base = io_base;
    uart->hardware_flow_control = hardware_flow_control;

    /* Disable UART interrupts: HCI framing is drained by the scheduler's
     * poll path, so no unregistered IRQ handler can consume its FIFO. */
    uart_outb(uart_port(uart, UART_REG_IER), 0U);
    uart_outb(uart_port(uart, UART_REG_LCR), UART_LCR_DLAB);
    uart_outb(uart_port(uart, UART_REG_DATA), (uint8_t)divisor);
    uart_outb(uart_port(uart, UART_REG_IER), (uint8_t)(divisor >> 8));
    uart_outb(uart_port(uart, UART_REG_LCR), 0x03U); /* 8 data, no parity, 1 stop */
    uart_outb(uart_port(uart, UART_REG_FCR), 0xC7U); /* FIFO, clear, 14-byte trigger */
    uart_outb(uart_port(uart, UART_REG_MCR),
              hardware_flow_control ? 0x0BU : 0x03U); /* DTR, RTS, OUT2 */

    /* Discard stale bytes from a previous firmware/bootloader UART session. */
    for (uint32_t i = 0U; i < 64U; ++i) {
        if ((uart_inb(uart_port(uart, UART_REG_LSR)) &
             UART_LSR_DATA_READY) == 0U) break;
        (void)uart_inb(uart_port(uart, UART_REG_DATA));
    }
    uart->initialized = true;
    return true;
}

static bool uart_wait_transmit_ready(struct nexus_bt_uart *uart,
                                     uint32_t spin_limit) {
    if (uart == NULL || spin_limit == 0U) return false;
    for (uint32_t spin = 0U; spin < spin_limit; ++spin) {
        const uint8_t status = uart_inb(uart_port(uart, UART_REG_LSR));
        if ((status & UART_LSR_THR_EMPTY) == 0U) continue;
        if (uart->hardware_flow_control &&
            (uart_inb(uart_port(uart, UART_REG_MSR)) & UART_MSR_CTS) == 0U)
            continue;
        return true;
    }
    return false;
}

static bool uart_validate_tx_packet(const uint8_t *packet, size_t length) {
    if (packet == NULL || length == 0U) return false;
    if (packet[0] == NEXUS_BT_HCI_PACKET_COMMAND) {
        if (length < 4U || packet[3] != length - 4U) return false;
        return (uint16_t)(packet[1] | ((uint16_t)packet[2] << 8)) != 0U;
    }
    if (packet[0] == NEXUS_BT_HCI_PACKET_ACL) {
        struct nexus_bt_hci_acl_view acl = {0};
        return nexus_bt_hci_parse_acl(packet, length, &acl);
    }
    return false;
}

bool nexus_bt_uart_send(struct nexus_bt_uart *uart,
    const uint8_t *packet, size_t packet_length, uint32_t spin_limit) {
    if (uart == NULL || !uart->initialized ||
        !uart_validate_tx_packet(packet, packet_length) ||
        spin_limit == 0U) return false;

    for (size_t i = 0U; i < packet_length; ++i) {
        if (!uart_wait_transmit_ready(uart, spin_limit)) return false;
        uart_outb(uart_port(uart, UART_REG_DATA), packet[i]);
    }
    return true;
}

static void uart_reset_rx(struct nexus_bt_uart *uart) {
    uart->rx_length = 0U;
    uart->rx_expected = 0U;
    uart->rx_complete = false;
}

static void uart_drop_rx(struct nexus_bt_uart *uart) {
    if (uart->rx_dropped_packets != UINT32_MAX)
        ++uart->rx_dropped_packets;
    uart_reset_rx(uart);
}

static bool uart_finish_rx_if_ready(struct nexus_bt_uart *uart) {
    if (uart->rx_expected == 0U ||
        uart->rx_length != uart->rx_expected) return false;

    if (uart->rx_packet[0] == NEXUS_BT_HCI_PACKET_EVENT) {
        struct nexus_bt_hci_event_view event = {0};
        if (!nexus_bt_hci_parse_event(uart->rx_packet,
                uart->rx_length, &event)) {
            uart_drop_rx(uart);
            return false;
        }
    } else {
        struct nexus_bt_hci_acl_view acl = {0};
        if (!nexus_bt_hci_parse_acl(uart->rx_packet,
                uart->rx_length, &acl)) {
            uart_drop_rx(uart);
            return false;
        }
    }
    uart->rx_complete = true;
    return true;
}

bool nexus_bt_uart_poll_receive(struct nexus_bt_uart *uart,
    uint8_t *packet, size_t packet_capacity, size_t *packet_length,
    size_t byte_budget) {
    if (uart == NULL || !uart->initialized || packet == NULL ||
        packet_length == NULL) return false;
    *packet_length = 0U;

    if (!uart->rx_complete) {
        for (size_t consumed = 0U; consumed < byte_budget; ++consumed) {
            if ((uart_inb(uart_port(uart, UART_REG_LSR)) &
                 UART_LSR_DATA_READY) == 0U) break;
            const uint8_t value = uart_inb(uart_port(uart, UART_REG_DATA));

            if (uart->rx_length == 0U) {
                if (value != NEXUS_BT_HCI_PACKET_EVENT &&
                    value != NEXUS_BT_HCI_PACKET_ACL) {
                    if (uart->rx_dropped_packets != UINT32_MAX)
                        ++uart->rx_dropped_packets;
                    continue;
                }
                uart->rx_packet[0] = value;
                uart->rx_length = 1U;
                continue;
            }

            if (uart->rx_length >= NEXUS_BT_UART_MAX_PACKET) {
                uart_drop_rx(uart);
                continue;
            }
            uart->rx_packet[uart->rx_length++] = value;

            if (uart->rx_packet[0] == NEXUS_BT_HCI_PACKET_EVENT &&
                uart->rx_length == 3U) {
                uart->rx_expected =
                    (uint16_t)(3U + uart->rx_packet[2]);
            } else if (uart->rx_packet[0] == NEXUS_BT_HCI_PACKET_ACL &&
                       uart->rx_length == 5U) {
                const uint16_t payload_length = (uint16_t)(
                    uart->rx_packet[3] |
                    ((uint16_t)uart->rx_packet[4] << 8));
                if (payload_length > NEXUS_BT_UART_MAX_ACL_PAYLOAD) {
                    uart_drop_rx(uart);
                    continue;
                }
                uart->rx_expected = (uint16_t)(5U + payload_length);
            }
            (void)uart_finish_rx_if_ready(uart);
            if (uart->rx_complete) break;
        }
    }

    if (!uart->rx_complete) return false;
    *packet_length = uart->rx_length;
    if (packet_capacity < uart->rx_length) return false;
    for (size_t i = 0U; i < uart->rx_length; ++i)
        packet[i] = uart->rx_packet[i];
    uart_reset_rx(uart);
    return true;
}
