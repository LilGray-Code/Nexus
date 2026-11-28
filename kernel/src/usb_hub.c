#include "usb_hub.h"

#include <string.h>

bool nexus_usb_hub_parse_descriptor(
    const uint8_t *descriptor, size_t length, bool multi_tt,
    struct nexus_usb_hub_descriptor *hub) {
    if (descriptor == NULL || hub == NULL || length < 7U ||
        descriptor[0] < 7U ||
        descriptor[1] != NEXUS_USB2_HUB_DESCRIPTOR_TYPE ||
        descriptor[0] > length) return false;

    /* The fixed USB 2 hub fields occupy bytes 0..6. DeviceRemovable
     * and PortPwrCtrlMask follow as bitmaps, but this driver does not consume
     * either bitmap. Some emulated hubs (including QEMU's 8-port hub) report
     * a shortened bitmap tail, so validate the fixed fields and descriptor
     * bounds without rejecting an otherwise usable hub solely for that tail. */
    const uint8_t ports = descriptor[2];
    if (ports == 0U || ports > NEXUS_USB_HUB_MAX_PORTS) return false;

    const uint16_t characteristics =
        (uint16_t)(descriptor[3] | ((uint16_t)descriptor[4] << 8));

    *hub = (struct nexus_usb_hub_descriptor){
        .port_count = ports,
        .characteristics = characteristics,
        .power_on_to_good_2ms = descriptor[5],
        .controller_current_ma = descriptor[6],
        .individual_port_power = (characteristics & 0x03U) == 1U,
        .no_power_switching = (characteristics & 0x03U) == 2U,
        .multi_tt = multi_tt
    };
    return true;
}


bool nexus_usb3_hub_parse_descriptor(
    const uint8_t *descriptor, size_t length,
    struct nexus_usb_hub_descriptor *hub) {
    if (descriptor == NULL || hub == NULL || length < 10U ||
        descriptor[0] < 10U ||
        descriptor[1] != NEXUS_USB3_HUB_DESCRIPTOR_TYPE ||
        descriptor[0] > length) return false;

    const uint8_t ports = descriptor[2];
    if (ports == 0U || ports > NEXUS_USB_HUB_MAX_PORTS) return false;

    const uint16_t characteristics =
        (uint16_t)(descriptor[3] | ((uint16_t)descriptor[4] << 8));
    const size_t removable_bytes = (size_t)(ports + 1U + 7U) / 8U;
    const size_t minimum_length = 10U + removable_bytes;
    if (descriptor[0] < minimum_length || length < minimum_length)
        return false;

    *hub = (struct nexus_usb_hub_descriptor){
        .port_count = ports,
        .characteristics = characteristics,
        .power_on_to_good_2ms = descriptor[5],
        .controller_current_ma = descriptor[6],
        .individual_port_power = (characteristics & 0x03U) == 1U,
        .no_power_switching = (characteristics & 0x03U) == 2U,
        .multi_tt = false,
        .superspeed = true,
        .header_decode_latency = descriptor[7],
        .hub_delay_ns = (uint16_t)(descriptor[8] |
            ((uint16_t)descriptor[9] << 8))
    };
    return true;
}


bool nexus_usb_hub_find_interrupt_endpoint(
    const uint8_t *configuration, size_t length, size_t minimum_packet_bytes,
    struct nexus_usb_hub_interrupt_endpoint *endpoint) {
    if (configuration == NULL || endpoint == NULL || length < 9U ||
        configuration[0] < 9U || configuration[1] != 2U ||
        minimum_packet_bytes == 0U || minimum_packet_bytes > 1024U)
        return false;
    const uint16_t total = (uint16_t)(configuration[2] |
        ((uint16_t)configuration[3] << 8));
    if (total < 9U || total > length || configuration[0] > total)
        return false;

    bool hub_interface = false;
    struct nexus_usb_hub_interrupt_endpoint candidate = {0};
    size_t offset = configuration[0];
    while (offset < total) {
        if (total - offset < 2U) return false;
        const uint8_t descriptor_length = configuration[offset];
        const uint8_t descriptor_type = configuration[offset + 1U];
        if (descriptor_length < 2U ||
            descriptor_length > total - offset) return false;

        if (descriptor_type == 4U) {
            if (descriptor_length < 9U) return false;
            hub_interface = configuration[offset + 5U] == 9U &&
                            configuration[offset + 3U] == 0U;
            candidate = (struct nexus_usb_hub_interrupt_endpoint){0};
        } else if (descriptor_type == 5U && hub_interface) {
            if (descriptor_length < 7U) return false;
            const uint8_t address = configuration[offset + 2U];
            const uint8_t attributes = configuration[offset + 3U] & 0x03U;
            const uint16_t raw_packet = (uint16_t)(
                configuration[offset + 4U] |
                ((uint16_t)configuration[offset + 5U] << 8));
            const uint16_t packet = raw_packet & 0x07FFU;
            const uint8_t interval = configuration[offset + 6U];
            if ((address & 0x80U) != 0U && (address & 0x0FU) != 0U &&
                attributes == 3U && packet >= minimum_packet_bytes &&
                packet <= 1024U && interval != 0U) {
                candidate.address = address;
                candidate.max_packet = packet;
                candidate.interval = interval;
            }
        }
        if (candidate.address != 0U) {
            *endpoint = candidate;
            return true;
        }
        offset += descriptor_length;
    }
    return false;
}

bool nexus_usb_hub_decode_change_bitmap(
    const uint8_t *bitmap, size_t bitmap_length, uint8_t port_count,
    uint16_t *changed_ports, bool *hub_status_changed) {
    if (bitmap == NULL || changed_ports == NULL ||
        hub_status_changed == NULL || port_count == 0U ||
        port_count > NEXUS_USB_HUB_MAX_PORTS) return false;
    const size_t required = ((size_t)port_count + 1U + 7U) / 8U;
    if (bitmap_length < required) return false;

    uint16_t ports = 0U;
    for (uint8_t port = 1U; port <= port_count; ++port) {
        const size_t byte_index = port / 8U;
        const uint8_t bit_index = (uint8_t)(port % 8U);
        if ((bitmap[byte_index] & (uint8_t)(1U << bit_index)) != 0U)
            ports |= (uint16_t)(1U << (port - 1U));
    }
    *changed_ports = ports;
    *hub_status_changed = (bitmap[0] & 0x01U) != 0U;
    return true;
}

bool nexus_usb_path_root(uint8_t root_port, struct nexus_usb_path *path) {
    if (root_port == 0U || path == NULL) return false;
    *path = (struct nexus_usb_path){
        .root_port = root_port,
        .parent_hub_slot = 0U,
        .parent_port = 0U,
        .depth = 0U,
        .route_string = 0U
    };
    return true;
}

bool nexus_usb_path_child(const struct nexus_usb_path *parent,
                          uint8_t hub_slot, uint8_t child_port,
                          struct nexus_usb_path *child) {
    if (parent == NULL || child == NULL || hub_slot == 0U ||
        child_port == 0U || child_port > 15U ||
        parent->root_port == 0U ||
        parent->depth >= NEXUS_USB_ROUTE_MAX_DEPTH ||
        (parent->route_string & ~0x000FFFFFU) != 0U) return false;

    const uint8_t shift = (uint8_t)(parent->depth * 4U);
    const uint32_t mask = 0x0FU << shift;
    if ((parent->route_string & mask) != 0U) return false;

    *child = (struct nexus_usb_path){
        .root_port = parent->root_port,
        .parent_hub_slot = hub_slot,
        .parent_port = child_port,
        .depth = (uint8_t)(parent->depth + 1U),
        .route_string = parent->route_string |
                        ((uint32_t)child_port << shift),
        .parent_hub_uses_tt = parent->parent_hub_uses_tt,
        .tt_hub_slot = parent->tt_hub_slot,
        .tt_port = parent->tt_port
    };
    return true;
}

bool nexus_usb_hub_decode_port_status(
    const uint8_t status[4], struct nexus_usb_hub_port_status *decoded) {
    if (status == NULL || decoded == NULL) return false;
    const uint16_t value =
        (uint16_t)(status[0] | ((uint16_t)status[1] << 8));
    const uint16_t change =
        (uint16_t)(status[2] | ((uint16_t)status[3] << 8));

    const bool connected = (value & (1U << 0)) != 0U;
    const bool low_speed = (value & (1U << 9)) != 0U;
    const bool high_speed = (value & (1U << 10)) != 0U;
    *decoded = (struct nexus_usb_hub_port_status){
        .connected = connected,
        .enabled = (value & (1U << 1)) != 0U,
        .resetting = (value & (1U << 4)) != 0U,
        .low_speed = low_speed,
        .high_speed = high_speed,
        .change_bits = change,
        .speed = low_speed ? NEXUS_USB_SPEED_LOW
            : high_speed ? NEXUS_USB_SPEED_HIGH
            : connected ? NEXUS_USB_SPEED_FULL : NEXUS_USB_SPEED_UNKNOWN
    };
    return true;
}

bool nexus_usb3_hub_decode_port_status(
    const uint8_t status[4], struct nexus_usb_hub_port_status *decoded) {
    if (status == NULL || decoded == NULL) return false;
    const uint16_t value =
        (uint16_t)(status[0] | ((uint16_t)status[1] << 8));
    const uint16_t change =
        (uint16_t)(status[2] | ((uint16_t)status[3] << 8));
    const uint8_t speed = (uint8_t)((value >> 10) & 0x07U);
    enum nexus_usb_speed decoded_speed;
    const bool connected = (value & (1U << 0)) != 0U;
    switch (speed) {
        case 0U:
            if (connected) return false;
            decoded_speed = NEXUS_USB_SPEED_UNKNOWN;
            break;
        case 1U: decoded_speed = NEXUS_USB_SPEED_LOW; break;
        case 2U: decoded_speed = NEXUS_USB_SPEED_HIGH; break;
        case 3U: decoded_speed = NEXUS_USB_SPEED_SUPER; break;
        case 4U: decoded_speed = NEXUS_USB_SPEED_SUPER_PLUS; break;
        default: return false;
    }
    *decoded = (struct nexus_usb_hub_port_status){
        .connected = connected,
        .enabled = (value & (1U << 1)) != 0U,
        .resetting = (value & (1U << 4)) != 0U,
        .low_speed = decoded_speed == NEXUS_USB_SPEED_LOW,
        .high_speed = decoded_speed == NEXUS_USB_SPEED_HIGH,
        .change_bits = change,
        .speed = decoded_speed
    };
    return true;
}
