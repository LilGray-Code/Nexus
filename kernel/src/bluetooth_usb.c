#include "bluetooth_usb.h"

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
            if (packet == 0U || (address & 0x70U) != 0U) {
                offset += dlen;
                continue;
            }
            if (attributes == 3U && (address & 0x80U) != 0U &&
                candidate.interrupt_in == 0U) {
                candidate.interrupt_in = address;
                candidate.interrupt_in_max_packet = packet & 0x07FFU;
                candidate.interrupt_interval = configuration[offset + 6U];
            } else if (attributes == 2U && (address & 0x80U) != 0U &&
                       candidate.bulk_in == 0U) {
                candidate.bulk_in = address;
                candidate.bulk_in_max_packet = packet & 0x07FFU;
            } else if (attributes == 2U && (address & 0x80U) == 0U &&
                       candidate.bulk_out == 0U) {
                candidate.bulk_out = address;
                candidate.bulk_out_max_packet = packet & 0x07FFU;
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
