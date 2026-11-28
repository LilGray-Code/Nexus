#include "bluetooth_usb.h"

#include "bluetooth_hci.h"

struct endpoint_candidate {
    uint8_t address;
    uint8_t attributes;
    uint16_t max_packet;
    uint8_t interval;
};

bool nexus_bluetooth_usb_find_interface(
    const uint8_t *configuration, size_t length,
    struct nexus_bluetooth_usb_interface *interface) {
    if (configuration == NULL || interface == NULL || length < 9U ||
        configuration[0] < 9U || configuration[1] != 2U) return false;
    const uint16_t total = (uint16_t)(configuration[2] |
        ((uint16_t)configuration[3] << 8));
    if (total < 9U || total > length || configuration[0] > total)
        return false;

    bool active = false;
    struct nexus_bluetooth_usb_interface candidate = {0};
    size_t offset = configuration[0];
    while (offset < total) {
        if (total - offset < 2U) return false;
        const uint8_t dlen = configuration[offset];
        const uint8_t dtype = configuration[offset + 1U];
        if (dlen < 2U || dlen > total - offset) return false;

        if (dtype == 4U) {
            if (dlen < 9U) return false;
            if (active) {
                if (candidate.interrupt_in != 0U &&
                    candidate.bulk_in != 0U &&
                    candidate.bulk_out != 0U) {
                    *interface = candidate;
                    return true;
                }
            }
            active = configuration[offset + 5U] == 0xE0U &&
                     configuration[offset + 6U] == 0x01U &&
                     configuration[offset + 7U] == 0x01U &&
                     configuration[offset + 3U] == 0U;
            candidate = (struct nexus_bluetooth_usb_interface){0};
            if (active) {
                candidate.interface_number = configuration[offset + 2U];
                candidate.alternate_setting = configuration[offset + 3U];
            }
        } else if (dtype == 5U && active) {
            if (dlen < 7U) return false;
            const uint8_t address = configuration[offset + 2U];
            const uint8_t attributes = configuration[offset + 3U] & 0x03U;
            const uint16_t packet = (uint16_t)(
                configuration[offset + 4U] |
                ((uint16_t)configuration[offset + 5U] << 8));
            const uint16_t max_packet = packet & 0x07FFU;
            if (max_packet == 0U || (address & 0x70U) != 0U ||
                (address & 0x0FU) == 0U) {
                offset += dlen;
                continue;
            }
            if (attributes == 3U && (address & 0x80U) != 0U &&
                candidate.interrupt_in == 0U) {
                candidate.interrupt_in = address;
                candidate.interrupt_in_max_packet = max_packet;
                candidate.interrupt_interval = configuration[offset + 6U];
            } else if (attributes == 2U && (address & 0x80U) != 0U &&
                       candidate.bulk_in == 0U) {
                candidate.bulk_in = address;
                candidate.bulk_in_max_packet = max_packet;
            } else if (attributes == 2U && (address & 0x80U) == 0U &&
                       candidate.bulk_out == 0U) {
                candidate.bulk_out = address;
                candidate.bulk_out_max_packet = max_packet;
            }
        }
        offset += dlen;
    }
    if (active && candidate.interrupt_in != 0U &&
        candidate.bulk_in != 0U && candidate.bulk_out != 0U) {
        *interface = candidate;
        return true;
    }
    return false;
}

bool nexus_bluetooth_usb_transport_init(
    struct nexus_bluetooth_usb_transport *transport,
    const struct nexus_bluetooth_usb_interface *interface,
    const struct nexus_bluetooth_usb_io *io) {
    if (transport == NULL || interface == NULL || io == NULL ||
        interface->interrupt_in == 0U ||
        (interface->interrupt_in & 0x80U) == 0U ||
        interface->bulk_in == 0U ||
        (interface->bulk_in & 0x80U) == 0U ||
        interface->bulk_out == 0U ||
        (interface->bulk_out & 0x80U) != 0U ||
        interface->interrupt_in_max_packet == 0U ||
        interface->bulk_in_max_packet == 0U ||
        interface->bulk_out_max_packet == 0U ||
        io->control_out == NULL || io->interrupt_in == NULL ||
        io->bulk_in == NULL || io->bulk_out == NULL)
        return false;
    *transport = (struct nexus_bluetooth_usb_transport){
        .initialized = true,
        .interface = *interface,
        .io = *io
    };
    return true;
}

bool nexus_bluetooth_usb_send_hci_command(
    struct nexus_bluetooth_usb_transport *transport,
    const uint8_t *hci_packet, size_t packet_length) {
    if (transport == NULL || !transport->initialized ||
        hci_packet == NULL || packet_length < 4U ||
        hci_packet[0] != NEXUS_BT_HCI_PACKET_COMMAND ||
        hci_packet[3] != packet_length - 4U)
        return false;
    /* Bluetooth USB class request: HCI Command, host-to-interface. */
    return transport->io.control_out(
        transport->io.context, 0x20U, 0x00U, 0U,
        transport->interface.interface_number,
        hci_packet + 1U, packet_length - 1U);
}

bool nexus_bluetooth_usb_read_hci_event(
    struct nexus_bluetooth_usb_transport *transport,
    uint8_t *hci_packet, size_t capacity, size_t *packet_length) {
    if (transport == NULL || !transport->initialized ||
        hci_packet == NULL || packet_length == NULL || capacity < 3U)
        return false;
    uint8_t event[NEXUS_BLUETOOTH_USB_MAX_EVENT_BYTES];
    size_t received = 0U;
    if (!transport->io.interrupt_in(transport->io.context,
            transport->interface.interrupt_in,
            event, sizeof(event), &received) ||
        received < 2U || received > sizeof(event) ||
        event[1] != received - 2U ||
        capacity < received + 1U)
        return false;
    hci_packet[0] = NEXUS_BT_HCI_PACKET_EVENT;
    for (size_t i = 0U; i < received; ++i)
        hci_packet[i + 1U] = event[i];
    *packet_length = received + 1U;
    return true;
}

bool nexus_bluetooth_usb_send_acl(
    struct nexus_bluetooth_usb_transport *transport,
    const uint8_t *hci_packet, size_t packet_length) {
    if (transport == NULL || !transport->initialized ||
        hci_packet == NULL || packet_length < 5U ||
        hci_packet[0] != NEXUS_BT_HCI_PACKET_ACL ||
        ((size_t)hci_packet[3] | ((size_t)hci_packet[4] << 8)) !=
            packet_length - 5U ||
        packet_length - 1U > NEXUS_BLUETOOTH_USB_MAX_ACL_BYTES)
        return false;
    return transport->io.bulk_out(
        transport->io.context, transport->interface.bulk_out,
        hci_packet + 1U, packet_length - 1U);
}

bool nexus_bluetooth_usb_poll_acl(
    struct nexus_bluetooth_usb_transport *transport,
    struct nexus_bt_hci_l2cap_packet *completed,
    enum nexus_bt_hci_acl_reassembly_status *status) {
    if (transport == NULL || !transport->initialized ||
        completed == NULL || status == NULL) return false;
    *completed = (struct nexus_bt_hci_l2cap_packet){0};
    *status = NEXUS_BT_HCI_ACL_REASSEMBLY_INVALID;

    uint8_t acl[NEXUS_BLUETOOTH_USB_MAX_ACL_BYTES];
    size_t received = 0U;
    if (!transport->io.bulk_in(transport->io.context,
            transport->interface.bulk_in, acl, sizeof(acl), &received) ||
        received < 4U || received > sizeof(acl) ||
        ((size_t)acl[2] | ((size_t)acl[3] << 8)) != received - 4U)
        return false;

    uint8_t framed[NEXUS_BLUETOOTH_USB_MAX_ACL_BYTES + 1U];
    framed[0] = NEXUS_BT_HCI_PACKET_ACL;
    for (size_t i = 0U; i < received; ++i) framed[i + 1U] = acl[i];
    struct nexus_bt_hci_acl_view fragment = {0};
    if (!nexus_bt_hci_parse_acl(framed, received + 1U, &fragment))
        return false;
    *status = nexus_bt_hci_acl_reassemble(
        &transport->acl_reassembler, &fragment, completed);
    return *status != NEXUS_BT_HCI_ACL_REASSEMBLY_INVALID;
}
