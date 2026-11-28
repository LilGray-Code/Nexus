#include "bluetooth_uart_host.h"

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
    if (reg == 5U)
        return (uint8_t)(0x20U | (rx_head != rx_tail ? 0x01U : 0U));
    if (reg == 6U) return 0x10U;
    if (reg == 0U && (registers[3] & 0x80U) == 0U) {
        assert(rx_head != rx_tail);
        return rx_fifo[rx_head++ % RX_FIFO_CAPACITY];
    }
    if (reg == 0U && (registers[3] & 0x80U) != 0U) return dll;
    if (reg == 1U && (registers[3] & 0x80U) != 0U) return dlm;
    return registers[reg];
}

void nexus_bt_uart_test_outb(uint16_t port, uint8_t value) {
    const uint16_t reg = (uint16_t)(port - TEST_BASE);
    assert(reg < 8U);
    if (reg == 0U && (registers[3] & 0x80U) != 0U) { dll = value; return; }
    if (reg == 1U && (registers[3] & 0x80U) != 0U) { dlm = value; return; }
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

static void test_uart_hci_host_bringup_and_acl_bridge(void) {
    reset_mock();
    struct nexus_bt_uart_host host = {0};
    assert(nexus_bt_uart_host_init(&host, TEST_BASE, 1843200U,
        115200U, false, 8U));
    assert(host.hci.state == NEXUS_BT_HCI_HOST_RESET_IN_FLIGHT);
    const uint8_t reset_command[] = {0x01U,0x03U,0x0CU,0x00U};
    assert(tx_length == sizeof(reset_command));
    assert(memcmp(tx_fifo, reset_command, sizeof(reset_command)) == 0);

    const uint8_t reset_complete[] = {0x04U,0x0EU,0x04U,0x01U,0x03U,0x0CU,0x00U};
    feed_rx(reset_complete, sizeof(reset_complete));
    assert(nexus_bt_uart_host_poll(&host, 32U, 8U));
    assert(host.hci.state == NEXUS_BT_HCI_HOST_VERSION_IN_FLIGHT);
    assert(tx_length == 10U && tx_fifo[5] == 0x01U &&
           tx_fifo[6] == 0x10U);

    const uint8_t version_complete[] = {
        0x04U,0x0EU,0x0CU,0x01U,0x01U,0x10U,0x00U,
        0x09U,0x0BU,0x04U,0x09U,0x34U,0x12U,0x78U,0x56U
    };
    feed_rx(version_complete, sizeof(version_complete));
    assert(nexus_bt_uart_host_poll(&host, 32U, 8U));
    assert(host.hci.state == NEXUS_BT_HCI_HOST_ADDRESS_IN_FLIGHT);
    assert(host.hci.hci_version == 0x09U);
    assert(host.hci.lmp_version == 0x09U);
    assert(host.hci.manufacturer == 0x1234U);
    assert(host.hci.subversion == 0x5678U);
    assert(tx_length == 14U && tx_fifo[11] == 0x09U &&
           tx_fifo[12] == 0x10U);

    const uint8_t address_complete[] = {
        0x04U,0x0EU,0x0AU,0x01U,0x09U,0x10U,0x00U,
        1U,2U,3U,4U,5U,6U
    };
    feed_rx(address_complete, sizeof(address_complete));
    assert(nexus_bt_uart_host_poll(&host, 32U, 8U));
    assert(host.hci.state == NEXUS_BT_HCI_HOST_READY);
    assert(memcmp(host.hci.controller_address,
        (const uint8_t[]){1U,2U,3U,4U,5U,6U}, 6U) == 0);

    const uint8_t classic_connection_complete[] = {
        0x04U,0x03U,0x0BU,0x00U,0x34U,0x02U,
        1U,2U,3U,4U,5U,6U,0x01U,0x00U
    };
    feed_rx(classic_connection_complete,
            sizeof(classic_connection_complete));
    assert(nexus_bt_uart_host_poll(&host, 32U, 8U));
    assert(host.connections[0].active);
    assert(host.connections[0].link.handle == 0x0234U);

    const uint8_t acl[] = {
        0x02U,0x34U,0x22U,0x06U,0x00U,
        0x02U,0x00U,0x04U,0x00U,0xAAU,0xBBU
    };
    feed_rx(acl, sizeof(acl));
    assert(nexus_bt_uart_host_poll(&host, 32U, 8U));
    uint8_t pdu[32] = {0U};
    size_t pdu_length = 0U;
    uint16_t handle = 0U;
    assert(nexus_bt_uart_host_read_l2cap(
        &host, &handle, pdu, sizeof(pdu), &pdu_length));
    assert(handle == 0x0234U && pdu_length == 6U);
    const uint8_t expected_pdu[] = {0x02U,0U,0x04U,0U,0xAAU,0xBBU};
    assert(memcmp(pdu, expected_pdu, sizeof(expected_pdu)) == 0);

    const size_t tx_start = tx_length;
    assert(nexus_bt_uart_host_send_acl(&host, 0x0234U, 2U,
        expected_pdu, sizeof(expected_pdu), 8U));
    const uint8_t expected_acl[] = {
        0x02U,0x34U,0x22U,0x06U,0x00U,
        0x02U,0x00U,0x04U,0x00U,0xAAU,0xBBU
    };
    assert(tx_length == tx_start + sizeof(expected_acl));
    assert(memcmp(tx_fifo + tx_start, expected_acl,
        sizeof(expected_acl)) == 0);
}

int main(void) {
    test_uart_hci_host_bringup_and_acl_bridge();
    puts("NEXUS BLUETOOTH UART HCI HOST BRING-UP AND ACL BRIDGE TESTS PASSED");
    return 0;
}
