#include "usb_hub.h"

#include <string.h>

bool nexus_usb_hub_parse_descriptor(
    const uint8_t *descriptor, size_t length, bool multi_tt,
    struct nexus_usb_hub_descriptor *hub) {
    if (descriptor == NULL || hub == NULL || length < 7U ||
        descriptor[0] < 7U ||
        descriptor[1] != NEXUS_USB2_HUB_DESCRIPTOR_TYPE ||
        descriptor[0] > length) return false;

    const uint8_t ports = descriptor[2];
    if (ports == 0U || ports > NEXUS_USB_HUB_MAX_PORTS) return false;

    const uint16_t characteristics =
        (uint16_t)(descriptor[3] | ((uint16_t)descriptor[4] << 8));
    const uint8_t bitmap_bytes = (uint8_t)((ports + 1U + 7U) / 8U);
    const size_t minimum_length = 7U + 2U * bitmap_bytes;
    if (descriptor[0] < minimum_length || length < minimum_length)
        return false;

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
                        ((uint32_t)child_port << shift)
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

    *decoded = (struct nexus_usb_hub_port_status){
        .connected = (value & (1U << 0)) != 0U,
        .enabled = (value & (1U << 1)) != 0U,
        .resetting = (value & (1U << 4)) != 0U,
        .low_speed = (value & (1U << 9)) != 0U,
        .high_speed = (value & (1U << 10)) != 0U,
        .change_bits = change,
        .speed = (value & (1U << 9)) != 0U
            ? NEXUS_USB_SPEED_LOW
            : (value & (1U << 10)) != 0U
                ? NEXUS_USB_SPEED_HIGH : NEXUS_USB_SPEED_FULL
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
    switch (speed) {
        case 0U: decoded_speed = NEXUS_USB_SPEED_FULL; break;
        case 1U: decoded_speed = NEXUS_USB_SPEED_LOW; break;
        case 2U: decoded_speed = NEXUS_USB_SPEED_HIGH; break;
        case 3U: decoded_speed = NEXUS_USB_SPEED_SUPER; break;
        case 4U: decoded_speed = NEXUS_USB_SPEED_SUPER_PLUS; break;
        default: return false;
    }
    *decoded = (struct nexus_usb_hub_port_status){
        .connected = (value & (1U << 0)) != 0U,
        .enabled = (value & (1U << 1)) != 0U,
        .resetting = (value & (1U << 4)) != 0U,
        .low_speed = decoded_speed == NEXUS_USB_SPEED_LOW,
        .high_speed = decoded_speed == NEXUS_USB_SPEED_HIGH,
        .change_bits = change,
        .speed = decoded_speed
    };
    return true;
}
