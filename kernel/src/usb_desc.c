#include "usb_desc.h"

bool nexus_usb_find_bulk_interface(
    const uint8_t *configuration, size_t length,
    uint8_t class_code, uint8_t subclass, uint8_t protocol,
    struct nexus_usb_bulk_interface *result) {
    if (configuration == NULL || result == NULL || length < 9U ||
        configuration[0] < 9U || configuration[1] != 2U) return false;

    const size_t total = (size_t)configuration[2] |
                         ((size_t)configuration[3] << 8);
    if (total < 9U || total > length || configuration[0] > total ||
        configuration[5] == 0U) return false;

    struct nexus_usb_bulk_interface candidate = {0};
    bool have_interface = false;
    uint8_t expected_endpoints = 0U;
    uint8_t observed_endpoints = 0U;
    size_t offset = 0U;
    while (offset < total) {
        if (total - offset < 2U) return false;
        const uint8_t descriptor_length = configuration[offset];
        const uint8_t descriptor_type = configuration[offset + 1U];
        if (descriptor_length < 2U ||
            (size_t)descriptor_length > total - offset) return false;

        if (descriptor_type == 4U) {
            if (descriptor_length < 9U) return false;
            if (have_interface && observed_endpoints == expected_endpoints &&
                candidate.bulk_in_endpoint != 0U &&
                candidate.bulk_out_endpoint != 0U) {
                *result = candidate;
                return true;
            }

            const uint8_t cls = configuration[offset + 5U];
            const uint8_t sub = configuration[offset + 6U];
            const uint8_t proto = configuration[offset + 7U];
            expected_endpoints = configuration[offset + 4U];
            observed_endpoints = 0U;
            have_interface =
                (class_code == NEXUS_USB_DESC_ANY || cls == class_code) &&
                (subclass == NEXUS_USB_DESC_ANY || sub == subclass) &&
                (protocol == NEXUS_USB_DESC_ANY || proto == protocol);
            candidate = (struct nexus_usb_bulk_interface){0};
            if (have_interface) {
                candidate.configuration_value = configuration[5];
                candidate.interface_number = configuration[offset + 2U];
                candidate.alternate_setting = configuration[offset + 3U];
                candidate.interface_class = cls;
                candidate.interface_subclass = sub;
                candidate.interface_protocol = proto;
            }
        } else if (descriptor_type == 5U && have_interface) {
            if (descriptor_length < 7U) return false;
            if (observed_endpoints == UINT8_MAX) return false;
            ++observed_endpoints;
            const uint8_t address = configuration[offset + 2U];
            const uint8_t attributes = configuration[offset + 3U] & 0x03U;
            const uint16_t packet = (uint16_t)(
                configuration[offset + 4U] |
                ((uint16_t)(configuration[offset + 5U] & 0x07U) << 8));
            /* Bulk wMaxPacketSize reserves bits 11..15. */
            if ((configuration[offset + 5U] & 0xF8U) != 0U) return false;
            const uint8_t endpoint_number = address & 0x0FU;
            const bool valid_packet = packet == 8U || packet == 16U ||
                packet == 32U || packet == 64U || packet == 512U ||
                packet == 1024U;
            if (endpoint_number == 0U || (address & 0x70U) != 0U)
                return false;
            if (attributes == 2U && valid_packet) {
                if ((address & 0x80U) != 0U &&
                    (address & 0x0FU) != 0U &&
                    candidate.bulk_in_endpoint == 0U) {
                    candidate.bulk_in_endpoint = address;
                    candidate.bulk_in_max_packet = packet;
                } else if ((address & 0x80U) == 0U &&
                           (address & 0x0FU) != 0U &&
                           candidate.bulk_out_endpoint == 0U) {
                    candidate.bulk_out_endpoint = address;
                    candidate.bulk_out_max_packet = packet;
                }
            }
        }
        offset += descriptor_length;
    }

    if (have_interface && observed_endpoints == expected_endpoints &&
        candidate.bulk_in_endpoint != 0U &&
        candidate.bulk_out_endpoint != 0U) {
        *result = candidate;
        return true;
    }
    return false;
}


static struct nexus_usb_device_info usb_inventory[
    NEXUS_USB_INVENTORY_MAX_DEVICES];
static size_t usb_inventory_count_value;

static uint16_t usb_read_le16(const uint8_t *value) {
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

bool nexus_usb_parse_device_descriptor(
    uint8_t port, const uint8_t *descriptor, size_t length,
    struct nexus_usb_device_info *result) {
    if (port == 0U || descriptor == NULL || result == NULL ||
        length < 18U || descriptor[0] != 18U || descriptor[1] != 1U)
        return false;

    const uint16_t vendor = usb_read_le16(descriptor + 8U);
    const uint16_t product = usb_read_le16(descriptor + 10U);
    const uint8_t packet_zero = descriptor[7];
    if (vendor == 0U || vendor == 0xFFFFU ||
        product == 0U || packet_zero == 0U) return false;

    struct nexus_usb_device_info parsed = {0};
    parsed.port = port;
    parsed.device_class = descriptor[4];
    parsed.device_subclass = descriptor[5];
    parsed.device_protocol = descriptor[6];
    parsed.vendor_id = vendor;
    parsed.product_id = product;
    parsed.device_revision = usb_read_le16(descriptor + 12U);
    *result = parsed;
    return true;
}

void nexus_usb_inventory_reset(void) {
    usb_inventory_count_value = 0U;
    for (size_t i = 0U; i < NEXUS_USB_INVENTORY_MAX_DEVICES; ++i)
        usb_inventory[i] = (struct nexus_usb_device_info){0};
}

static size_t usb_inventory_find_port(uint8_t port) {
    for (size_t i = 0U; i < usb_inventory_count_value; ++i) {
        if (usb_inventory[i].port == port) return i;
    }
    return NEXUS_USB_INVENTORY_MAX_DEVICES;
}

bool nexus_usb_inventory_add_device(
    uint8_t port, const uint8_t *descriptor, size_t length) {
    struct nexus_usb_device_info parsed;
    if (!nexus_usb_parse_device_descriptor(port, descriptor, length, &parsed))
        return false;

    size_t index = usb_inventory_find_port(port);
    if (index == NEXUS_USB_INVENTORY_MAX_DEVICES) {
        if (usb_inventory_count_value >= NEXUS_USB_INVENTORY_MAX_DEVICES)
            return false;
        index = usb_inventory_count_value++;
    }
    usb_inventory[index] = parsed;
    return true;
}

bool nexus_usb_inventory_set_configuration(
    uint8_t port, const uint8_t *configuration, size_t length) {
    const size_t index = usb_inventory_find_port(port);
    if (index == NEXUS_USB_INVENTORY_MAX_DEVICES ||
        configuration == NULL || length < 9U ||
        configuration[0] < 9U || configuration[1] != 2U) return false;

    const size_t total = usb_read_le16(configuration + 2U);
    if (total < 9U || total > length || total > 4096U ||
        configuration[5] == 0U) return false;

    struct nexus_usb_device_info parsed = usb_inventory[index];
    parsed.network_kind = NEXUS_USB_NETWORK_NONE;
    parsed.network_interface_number = 0U;
    parsed.network_interface_class = 0U;
    parsed.network_interface_subclass = 0U;
    parsed.network_interface_protocol = 0U;
    parsed.has_vendor_specific_interface = false;

    size_t offset = 0U;
    while (offset < total) {
        if (total - offset < 2U) return false;
        const uint8_t descriptor_length = configuration[offset];
        const uint8_t descriptor_type = configuration[offset + 1U];
        if (descriptor_length < 2U ||
            (size_t)descriptor_length > total - offset) return false;

        if (descriptor_type == 4U) {
            if (descriptor_length < 9U) return false;
            const uint8_t number = configuration[offset + 2U];
            const uint8_t class_code = configuration[offset + 5U];
            const uint8_t subclass = configuration[offset + 6U];
            const uint8_t protocol = configuration[offset + 7U];

            if (class_code == 0xFFU)
                parsed.has_vendor_specific_interface = true;

            if (parsed.network_kind == NEXUS_USB_NETWORK_NONE) {
                enum nexus_usb_network_interface_kind kind =
                    NEXUS_USB_NETWORK_NONE;
                if (class_code == 0x02U && subclass == 0x06U)
                    kind = NEXUS_USB_NETWORK_CDC_ECM;
                else if (class_code == 0x02U && subclass == 0x0DU)
                    kind = NEXUS_USB_NETWORK_CDC_NCM;
                else if (class_code == 0x02U && subclass == 0x0EU)
                    kind = NEXUS_USB_NETWORK_MBIM;
                else if (class_code == 0xE0U && subclass == 0x01U &&
                         protocol == 0x03U)
                    kind = NEXUS_USB_NETWORK_RNDIS;

                if (kind != NEXUS_USB_NETWORK_NONE) {
                    parsed.network_kind = kind;
                    parsed.network_interface_number = number;
                    parsed.network_interface_class = class_code;
                    parsed.network_interface_subclass = subclass;
                    parsed.network_interface_protocol = protocol;
                }
            }
        }
        offset += descriptor_length;
    }

    usb_inventory[index] = parsed;
    return true;
}

size_t nexus_usb_inventory_count(void) {
    return usb_inventory_count_value;
}

bool nexus_usb_inventory_get(size_t index,
                             struct nexus_usb_device_info *result) {
    if (result == NULL || index >= usb_inventory_count_value) return false;
    *result = usb_inventory[index];
    return true;
}
