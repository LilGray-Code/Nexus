#include "usb_desc.h"

bool nexus_usb_find_bulk_interface(
    const uint8_t *configuration, size_t length,
    uint8_t class_code, uint8_t subclass, uint8_t protocol,
    struct nexus_usb_bulk_interface *result) {
    if (configuration == NULL || result == NULL || length < 9U ||
        configuration[0] < 9U || configuration[1] != 2U) return false;

    const size_t total = (size_t)configuration[2] |
                         ((size_t)configuration[3] << 8);
    if (total < 9U || total > length) return false;

    struct nexus_usb_bulk_interface candidate = {0};
    bool have_interface = false;
    size_t offset = 0U;
    while (offset < total) {
        if (total - offset < 2U) return false;
        const uint8_t descriptor_length = configuration[offset];
        const uint8_t descriptor_type = configuration[offset + 1U];
        if (descriptor_length < 2U ||
            (size_t)descriptor_length > total - offset) return false;

        if (descriptor_type == 4U) {
            if (descriptor_length < 9U) return false;
            if (have_interface && candidate.bulk_in_endpoint != 0U &&
                candidate.bulk_out_endpoint != 0U) {
                *result = candidate;
                return true;
            }

            const uint8_t cls = configuration[offset + 5U];
            const uint8_t sub = configuration[offset + 6U];
            const uint8_t proto = configuration[offset + 7U];
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
            const uint8_t address = configuration[offset + 2U];
            const uint8_t attributes = configuration[offset + 3U] & 0x03U;
            const uint16_t packet = (uint16_t)(
                configuration[offset + 4U] |
                ((uint16_t)(configuration[offset + 5U] & 0x07U) << 8));
            if (attributes == 2U && packet != 0U) {
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

    if (have_interface && candidate.bulk_in_endpoint != 0U &&
        candidate.bulk_out_endpoint != 0U) {
        *result = candidate;
        return true;
    }
    return false;
}
