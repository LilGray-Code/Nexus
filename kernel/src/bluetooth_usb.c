#include "bluetooth_usb.h"

struct endpoint_candidate {
    bool active;
    bool malformed;
    uint8_t declared_endpoints;
    uint8_t observed_endpoints;
    struct nexus_bluetooth_usb_interface interface;
};

static bool endpoint_candidate_complete(
    const struct endpoint_candidate *candidate) {
    return candidate != NULL && candidate->active &&
        !candidate->malformed &&
        candidate->declared_endpoints >= 3U &&
        candidate->observed_endpoints == candidate->declared_endpoints &&
        candidate->interface.interrupt_in != 0U &&
        candidate->interface.bulk_in != 0U &&
        candidate->interface.bulk_out != 0U;
}

static bool endpoint_address_unique(
    const struct nexus_bluetooth_usb_interface *interface,
    uint8_t address) {
    return address != interface->interrupt_in &&
           address != interface->bulk_in &&
           address != interface->bulk_out;
}

bool nexus_bluetooth_usb_find_interface(
    const uint8_t *configuration, size_t length,
    struct nexus_bluetooth_usb_interface *interface) {
    if (configuration == NULL || interface == NULL || length < 9U ||
        configuration[0] < 9U || configuration[1] != 2U) return false;
    const uint16_t total = (uint16_t)(configuration[2] |
        ((uint16_t)configuration[3] << 8));
    if (total < 9U || total > length || configuration[0] > total)
        return false;

    struct endpoint_candidate candidate = {0};
    size_t offset = configuration[0];
    while (offset < total) {
        if (total - offset < 2U) return false;
        const uint8_t descriptor_length = configuration[offset];
        const uint8_t descriptor_type = configuration[offset + 1U];
        if (descriptor_length < 2U ||
            descriptor_length > total - offset) return false;

        if (descriptor_type == 4U) {
            if (descriptor_length < 9U) return false;
            if (endpoint_candidate_complete(&candidate)) {
                *interface = candidate.interface;
                return true;
            }
            candidate = (struct endpoint_candidate){0};
            const bool bluetooth_class =
                configuration[offset + 5U] == 0xE0U &&
                configuration[offset + 6U] == 0x01U &&
                configuration[offset + 7U] == 0x01U;
            const bool default_alternate =
                configuration[offset + 3U] == 0U;
            if (bluetooth_class && default_alternate) {
                candidate.active = true;
                candidate.declared_endpoints = configuration[offset + 4U];
                candidate.interface.interface_number =
                    configuration[offset + 2U];
                candidate.interface.alternate_setting =
                    configuration[offset + 3U];
                if (candidate.declared_endpoints < 3U)
                    candidate.malformed = true;
            }
        } else if (descriptor_type == 5U && candidate.active) {
            if (descriptor_length < 7U) return false;
            if (candidate.observed_endpoints != UINT8_MAX)
                ++candidate.observed_endpoints;
            else
                candidate.malformed = true;

            const uint8_t address = configuration[offset + 2U];
            const uint8_t raw_attributes = configuration[offset + 3U];
            const uint8_t attributes = raw_attributes & 0x03U;
            const uint16_t raw_packet = (uint16_t)(
                configuration[offset + 4U] |
                ((uint16_t)configuration[offset + 5U] << 8));
            const uint16_t packet = raw_packet & 0x07FFU;
            const uint8_t interval = configuration[offset + 6U];

            /* Endpoint zero, reserved address bits, reserved attribute bits,
             * and reserved wMaxPacketSize high bits are not valid data pipes. */
            if ((address & 0x70U) != 0U || (address & 0x0FU) == 0U ||
                (raw_attributes & 0xFCU) != 0U ||
                (raw_packet & 0xE000U) != 0U || packet == 0U) {
                candidate.malformed = true;
                offset += descriptor_length;
                continue;
            }
            if (!endpoint_address_unique(&candidate.interface, address)) {
                candidate.malformed = true;
                offset += descriptor_length;
                continue;
            }

            if (attributes == 3U && (address & 0x80U) != 0U) {
                /* High-bandwidth interrupt transactions are not yet supported
                 * by the USB Bluetooth consumer, so reject rather than silently
                 * under-size the scheduled endpoint. */
                if (interval == 0U || packet > 64U ||
                    (raw_packet & 0x1800U) != 0U ||
                    candidate.interface.interrupt_in != 0U) {
                    candidate.malformed = true;
                } else {
                    candidate.interface.interrupt_in = address;
                    candidate.interface.interrupt_in_max_packet = packet;
                    candidate.interface.interrupt_interval = interval;
                }
            } else if (attributes == 2U &&
                       (raw_packet & 0x1800U) == 0U && packet <= 512U &&
                       (address & 0x80U) != 0U) {
                if (candidate.interface.bulk_in != 0U) {
                    candidate.malformed = true;
                } else {
                    candidate.interface.bulk_in = address;
                    candidate.interface.bulk_in_max_packet = packet;
                }
            } else if (attributes == 2U &&
                       (raw_packet & 0x1800U) == 0U && packet <= 512U &&
                       (address & 0x80U) == 0U) {
                if (candidate.interface.bulk_out != 0U) {
                    candidate.malformed = true;
                } else {
                    candidate.interface.bulk_out = address;
                    candidate.interface.bulk_out_max_packet = packet;
                }
            }
        }
        offset += descriptor_length;
    }

    if (endpoint_candidate_complete(&candidate)) {
        *interface = candidate.interface;
        return true;
    }
    return false;
}
