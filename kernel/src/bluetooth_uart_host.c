#include "bluetooth_uart_host.h"

static void host_drop_count(struct nexus_bt_uart_host *host) {
    if (host->dropped_packets != UINT32_MAX) ++host->dropped_packets;
}

static struct nexus_bt_uart_host_connection *host_find_connection(
    struct nexus_bt_uart_host *host, uint16_t handle) {
    if (host == NULL || handle == 0U) return NULL;
    for (size_t i = 0U; i < NEXUS_BT_UART_HOST_MAX_CONNECTIONS; ++i) {
        struct nexus_bt_uart_host_connection *connection =
            &host->connections[i];
        if (connection->active && connection->link.handle == handle)
            return connection;
    }
    return NULL;
}

static bool host_add_connection(
    struct nexus_bt_uart_host *host,
    const struct nexus_bt_hci_connection *link) {
    if (host == NULL || link == NULL || !link->connected ||
        link->handle == 0U || link->handle > 0x0FFFU) return false;
    struct nexus_bt_uart_host_connection *connection =
        host_find_connection(host, link->handle);
    if (connection == NULL) {
        for (size_t i = 0U; i < NEXUS_BT_UART_HOST_MAX_CONNECTIONS; ++i) {
            if (!host->connections[i].active) {
                connection = &host->connections[i];
                break;
            }
        }
    }
    if (connection == NULL) return false;
    *connection = (struct nexus_bt_uart_host_connection){0};
    connection->active = true;
    connection->link = *link;
    return true;
}

static void host_remove_connection(struct nexus_bt_uart_host *host,
                                   uint16_t handle) {
    struct nexus_bt_uart_host_connection *connection =
        host_find_connection(host, handle);
    if (connection != NULL)
        *connection = (struct nexus_bt_uart_host_connection){0};
}

static bool host_send_next_command(struct nexus_bt_uart_host *host,
                                   uint32_t spin_limit) {
    uint8_t packet[260];
    size_t packet_length = 0U;
    if (!nexus_bt_hci_host_next_command(&host->hci, packet,
            sizeof(packet), &packet_length)) return true;

    if (nexus_bt_uart_send(&host->uart, packet, packet_length, spin_limit))
        return true;

    /* UART send can fail after a prefix was transmitted (for example, CTS
     * drops in the middle of an H4 frame). Do not retry a partial HCI command:
     * the byte stream must be reset by the owner before another packet. */
    host->hci.state = NEXUS_BT_HCI_HOST_FAILED;
    host->hci.outstanding_opcode = 0U;
    return false;
}

static bool host_queue_completed_l2cap(
    struct nexus_bt_uart_host *host,
    struct nexus_bt_uart_host_connection *connection) {
    const uint8_t next = (uint8_t)(
        (host->queue_tail + 1U) % NEXUS_BT_UART_HOST_PDU_QUEUE_DEPTH);
    if (next == host->queue_head) {
        host_drop_count(host);
        nexus_bt_acl_reassembler_reset(&connection->reassembler);
        return false;
    }

    struct nexus_bt_uart_host_pdu *item =
        &host->receive_queue[host->queue_tail];
    size_t length = 0U;
    if (!nexus_bt_acl_reassembler_read(&connection->reassembler,
            item->bytes, sizeof(item->bytes), &length) ||
        length > UINT16_MAX) {
        host_drop_count(host);
        return false;
    }
    item->connection_handle = connection->link.handle;
    item->length = (uint16_t)length;
    host->queue_tail = next;
    return true;
}

static void host_process_event(struct nexus_bt_uart_host *host,
                               const struct nexus_bt_hci_event_view *event) {
    if (host == NULL || event == NULL) return;

    if (host->hci.state != NEXUS_BT_HCI_HOST_READY &&
        host->hci.state != NEXUS_BT_HCI_HOST_FAILED) {
        if (event->event_code == NEXUS_BT_HCI_EVENT_COMMAND_COMPLETE ||
            event->event_code == NEXUS_BT_HCI_EVENT_COMMAND_STATUS)
            (void)nexus_bt_hci_host_handle_event(&host->hci, event);
    }

    if (host->hci.state != NEXUS_BT_HCI_HOST_READY) return;

    struct nexus_bt_hci_connection link = {0};
    if (nexus_bt_hci_connection_complete(event, &link)) {
        if (!host_add_connection(host, &link)) host_drop_count(host);
        return;
    }
    uint16_t handle = 0U;
    uint8_t reason = 0U;
    if (nexus_bt_hci_disconnection_complete(event, &handle, &reason)) {
        (void)reason;
        host_remove_connection(host, handle);
    }
}

static void host_process_acl(struct nexus_bt_uart_host *host,
                             const uint8_t *packet, size_t packet_length) {
    struct nexus_bt_hci_acl_view acl = {0};
    if (!nexus_bt_hci_parse_acl(packet, packet_length, &acl)) {
        host_drop_count(host);
        return;
    }
    struct nexus_bt_uart_host_connection *connection =
        host_find_connection(host, acl.connection_handle);
    if (connection == NULL) {
        host_drop_count(host);
        return;
    }
    const enum nexus_bt_acl_reassembly_result result =
        nexus_bt_acl_reassembler_feed(&connection->reassembler, &acl);
    if (result == NEXUS_BT_ACL_REJECTED) {
        host_drop_count(host);
    } else if (result == NEXUS_BT_ACL_COMPLETE) {
        (void)host_queue_completed_l2cap(host, connection);
    }
}

bool nexus_bt_uart_host_init(struct nexus_bt_uart_host *host,
    uint16_t io_base, uint32_t input_clock_hz, uint32_t baud_rate,
    bool hardware_flow_control, uint32_t transmit_spin_limit) {
    if (host == NULL || transmit_spin_limit == 0U) return false;
    *host = (struct nexus_bt_uart_host){0};
    if (!nexus_bt_uart_init(&host->uart, io_base, input_clock_hz,
            baud_rate, hardware_flow_control)) return false;
    nexus_bt_hci_host_init(&host->hci);
    host->initialized = true;
    if (!host_send_next_command(host, transmit_spin_limit)) {
        host->initialized = false;
        return false;
    }
    return true;
}

bool nexus_bt_uart_host_poll(struct nexus_bt_uart_host *host,
    size_t byte_budget, uint32_t transmit_spin_limit) {
    if (host == NULL || !host->initialized || transmit_spin_limit == 0U)
        return false;

    size_t packet_length = 0U;
    const bool received = nexus_bt_uart_poll_receive(&host->uart,
        host->receive_packet, sizeof(host->receive_packet),
        &packet_length, byte_budget);
    if (!received) {
        (void)host_send_next_command(host, transmit_spin_limit);
        return false;
    }

    if (host->receive_packet[0] == NEXUS_BT_HCI_PACKET_EVENT) {
        struct nexus_bt_hci_event_view event = {0};
        if (nexus_bt_hci_parse_event(host->receive_packet, packet_length,
                                     &event))
            host_process_event(host, &event);
        else
            host_drop_count(host);
    } else if (host->receive_packet[0] == NEXUS_BT_HCI_PACKET_ACL) {
        if (host->hci.state == NEXUS_BT_HCI_HOST_READY)
            host_process_acl(host, host->receive_packet, packet_length);
        else
            host_drop_count(host);
    } else {
        host_drop_count(host);
    }

    (void)host_send_next_command(host, transmit_spin_limit);
    return true;
}

bool nexus_bt_uart_host_send_acl(struct nexus_bt_uart_host *host,
    uint16_t connection_handle, uint8_t packet_boundary,
    const uint8_t *payload, size_t payload_length,
    uint32_t transmit_spin_limit) {
    if (host == NULL || !host->initialized ||
        host->hci.state != NEXUS_BT_HCI_HOST_READY ||
        host_find_connection(host, connection_handle) == NULL ||
        connection_handle > 0x0FFFU || packet_boundary > 2U ||
        payload == NULL || payload_length == 0U ||
        payload_length > NEXUS_BT_ACL_MAX_L2CAP_PDU ||
        payload_length + 5U > NEXUS_BT_UART_MAX_PACKET) return false;

    uint8_t packet[NEXUS_BT_UART_MAX_PACKET];
    const uint16_t handle_flags = (uint16_t)(
        connection_handle | ((uint16_t)packet_boundary << 12));
    packet[0] = NEXUS_BT_HCI_PACKET_ACL;
    packet[1] = (uint8_t)handle_flags;
    packet[2] = (uint8_t)(handle_flags >> 8);
    packet[3] = (uint8_t)payload_length;
    packet[4] = (uint8_t)(payload_length >> 8);
    for (size_t i = 0U; i < payload_length; ++i)
        packet[5U + i] = payload[i];
    return nexus_bt_uart_send(&host->uart, packet,
        payload_length + 5U, transmit_spin_limit);
}

bool nexus_bt_uart_host_read_l2cap(struct nexus_bt_uart_host *host,
    uint16_t *connection_handle, uint8_t *pdu, size_t pdu_capacity,
    size_t *pdu_length) {
    if (host == NULL || connection_handle == NULL || pdu == NULL ||
        pdu_length == NULL || host->queue_head == host->queue_tail)
        return false;
    const struct nexus_bt_uart_host_pdu *item =
        &host->receive_queue[host->queue_head];
    *pdu_length = item->length;
    if (pdu_capacity < item->length) return false;
    *connection_handle = item->connection_handle;
    for (uint16_t i = 0U; i < item->length; ++i)
        pdu[i] = item->bytes[i];
    host->queue_head = (uint8_t)(
        (host->queue_head + 1U) % NEXUS_BT_UART_HOST_PDU_QUEUE_DEPTH);
    return true;
}
