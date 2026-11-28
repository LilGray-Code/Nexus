#include "bluetooth_uart.h"
#include "bluetooth_hci.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_BASE 0x3E8U
#define RX_FIFO_CAPACITY 2048U
#define TX_FIFO_CAPACITY 2048U

static uint8_t registers[8];
static uint8_t rx_fifo[RX_FIFO_CAPACITY];
static size_t rx_head, rx_tail;
static uint8_t tx_fifo[TX_FIFO_CAPACITY];
static size_t tx_length;
static uint8_t dll, dlm;

uint8_t nexus_bt_uart_test_inb(uint16_t port) {
    const uint16_t reg = (uint16_t)(port - TEST_BASE);
    assert(reg < 8U);
    if (reg == 5U) {
        return (uint8_t)(0x20U | (rx_head != rx_tail ? 0x01U : 0U));
    }
    if (reg == 6U) return 0x10U; /* CTS asserted. */
    if (reg == 0U && (registers[3] & 0x80U) == 0U) {
        assert(rx_head != rx_tail);
        const uint8_t value = rx_fifo[rx_head++ % RX_FIFO_CAPACITY];
        return value;
    }
    if (reg == 0U && (registers[3] & 0x80U) != 0U) return dll;
    if (reg == 1U && (registers[3] & 0x80U) != 0U) return dlm;
    return registers[reg];
}

void nexus_bt_uart_test_outb(uint16_t port, uint8_t value) {
    const uint16_t reg = (uint16_t)(port - TEST_BASE);
    assert(reg < 8U);
    if (reg == 0U && (registers[3] & 0x80U) != 0U) {
        dll = value;
        return;
    }
    if (reg == 1U && (registers[3] & 0x80U) != 0U) {
        dlm = value;
        return;
    }
    if (reg == 0U && (registers[3] & 0x80U) == 0U) {
        assert(tx_length < TX_FIFO_CAPACITY);
        tx_fifo[tx_length++] = value;
        return;
    }
    registers[reg] = value;
}

static void reset_mock(void) {
    memset(registers, 0, sizeof(registers));
    rx_head = rx_tail = tx_length = 0U;
    dll = dlm = 0U;
}

static void feed_rx(const uint8_t *bytes, size_t length) {
    assert(bytes != NULL && length <= RX_FIFO_CAPACITY -
        (rx_tail - rx_head));
    for (size_t i = 0U; i < length; ++i)
        rx_fifo[rx_tail++ % RX_FIFO_CAPACITY] = bytes[i];
}

static void test_uart_initialization(void) {
    reset_mock();
    struct nexus_bt_uart uart = {0};
    assert(nexus_bt_uart_init(&uart, TEST_BASE, 1843200U, 115200U, true));
    assert(uart.initialized && uart.io_base == TEST_BASE);
    assert(uart.hardware_flow_control);
    assert(dll == 1U && dlm == 0U);
    assert(registers[1] == 0U);
    assert(registers[3] == 0x03U);
    assert(registers[2] == 0xC7U);
    assert(registers[4] == 0x0BU);
    assert(!nexus_bt_uart_init(&uart, 0U, 1843200U, 115200U, false));
    assert(!nexus_bt_uart_init(&uart, TEST_BASE, 0U, 115200U, false));
}

static void test_h4_command_and_acl_transmit(void) {
    reset_mock();
    struct nexus_bt_uart uart = {0};
    assert(nexus_bt_uart_init(&uart, TEST_BASE, 1843200U, 115200U, false));

    const uint8_t command[] = {0x01U,0x03U,0x0CU,0x00U};
    assert(nexus_bt_uart_send(&uart, command, sizeof(command), 8U));
    assert(tx_length == sizeof(command));
    assert(memcmp(tx_fifo, command, sizeof(command)) == 0);

    tx_length = 0U;
    const uint8_t acl[] = {
        0x02U,0x01U,0x20U,0x03U,0x00U,0xAAU,0xBBU,0xCCU
    };
    assert(nexus_bt_uart_send(&uart, acl, sizeof(acl), 8U));
    assert(tx_length == sizeof(acl));
    assert(memcmp(tx_fifo, acl, sizeof(acl)) == 0);

    const uint8_t truncated[] = {0x01U,0x03U,0x0CU,0x02U};
    assert(!nexus_bt_uart_send(&uart, truncated, sizeof(truncated), 8U));
    const uint8_t event[] = {0x04U,0x0EU,0x00U};
    assert(!nexus_bt_uart_send(&uart, event, sizeof(event), 8U));
}

static void test_fragmented_event_receive(void) {
    reset_mock();
    struct nexus_bt_uart uart = {0};
    assert(nexus_bt_uart_init(&uart, TEST_BASE, 1843200U, 115200U, false));

    const uint8_t event[] = {0x04U,0x0EU,0x04U,0x01U,0x03U,0x0CU,0x00U};
    feed_rx(event, 3U);
    uint8_t packet[ NEXUS_BT_UART_MAX_PACKET ];
    size_t length = 0U;
    assert(!nexus_bt_uart_poll_receive(&uart, packet, sizeof(packet),
        &length, 16U));
    assert(length == 0U);
    feed_rx(event + 3U, sizeof(event) - 3U);
    assert(nexus_bt_uart_poll_receive(&uart, packet, sizeof(packet),
        &length, 16U));
    assert(length == sizeof(event));
    struct nexus_bt_hci_event_view parsed = {0};
    assert(nexus_bt_hci_parse_event(packet, length, &parsed));
    assert(parsed.event_code == 0x0EU && parsed.parameter_length == 4U);
}

static void test_acl_receive_and_small_buffer_retry(void) {
    reset_mock();
    struct nexus_bt_uart uart = {0};
    assert(nexus_bt_uart_init(&uart, TEST_BASE, 1843200U, 115200U, false));

    const uint8_t acl[] = {
        0x02U,0x34U,0x12U,0x04U,0x00U,0x01U,0x02U,0x03U,0x04U
    };
    feed_rx(acl, sizeof(acl));
    uint8_t small[4];
    size_t length = 0U;
    assert(!nexus_bt_uart_poll_receive(&uart, small, sizeof(small),
        &length, 32U));
    assert(length == sizeof(acl));
    uint8_t packet[NEXUS_BT_UART_MAX_PACKET];
    assert(nexus_bt_uart_poll_receive(&uart, packet, sizeof(packet),
        &length, 32U));
    assert(length == sizeof(acl));
    struct nexus_bt_hci_acl_view parsed = {0};
    assert(nexus_bt_hci_parse_acl(packet, length, &parsed));
    assert(parsed.connection_handle == 0x0234U);
    assert(parsed.payload_length == 4U);
}

static void test_malformed_acl_is_dropped_and_parser_resynchronizes(void) {
    reset_mock();
    struct nexus_bt_uart uart = {0};
    assert(nexus_bt_uart_init(&uart, TEST_BASE, 1843200U, 115200U, false));

    const uint8_t malformed[] = {0x02U,0x01U,0x20U,0x01U,0x04U};
    const uint8_t event[] = {0x04U,0x05U,0x00U};
    feed_rx(malformed, sizeof(malformed));
    feed_rx(event, sizeof(event));
    uint8_t packet[NEXUS_BT_UART_MAX_PACKET];
    size_t length = 0U;
    assert(nexus_bt_uart_poll_receive(&uart, packet, sizeof(packet),
        &length, 32U));
    assert(length == sizeof(event));
    assert(packet[0] == 0x04U && packet[1] == 0x05U);
    assert(uart.rx_dropped_packets == 1U);
}

int main(void) {
    test_uart_initialization();
    test_h4_command_and_acl_transmit();
    test_fragmented_event_receive();
    test_acl_receive_and_small_buffer_retry();
    test_malformed_acl_is_dropped_and_parser_resynchronizes();
    puts("NEXUS BLUETOOTH 16550 UART H4 TRANSPORT TESTS PASSED");
    return 0;
}
