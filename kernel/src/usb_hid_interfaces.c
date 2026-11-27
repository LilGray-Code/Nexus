#include "usb_hid_interfaces.h"

#include <string.h>

struct hid_interface_scan {
    bool active;
    bool is_hid;
    struct nexus_usb_hid_interface item;
};

static bool hid_finish_interface(
    const struct hid_interface_scan *scan,
    struct nexus_usb_hid_interface_set *set) {
    if (scan == NULL || set == NULL) return false;
    if (!scan->active || !scan->is_hid || scan->item.alternate_setting != 0U ||
        scan->item.endpoint_in == 0U ||
        scan->item.report_descriptor_length == 0U)
        return true;
    if (set->interface_count >= NEXUS_USB_HID_MAX_INTERFACES) return false;
    set->interfaces[set->interface_count++] = scan->item;
    return true;
}

bool nexus_usb_hid_collect_interfaces(
    const uint8_t *configuration, size_t length,
    struct nexus_usb_hid_interface_set *set) {
    if (configuration == NULL || set == NULL || length < 9U ||
        configuration[0] < 9U || configuration[1] != 2U)
        return false;

    const uint16_t total_length = (uint16_t)(
        configuration[2] | ((uint16_t)configuration[3] << 8));
    if (total_length < 9U || total_length > length ||
        configuration[0] > total_length || configuration[5] == 0U)
        return false;

    *set = (struct nexus_usb_hid_interface_set){0};
    set->configuration_value = configuration[5];
    struct hid_interface_scan current = {0};

    size_t offset = configuration[0];
    while (offset < total_length) {
        if (total_length - offset < 2U) return false;
        const uint8_t descriptor_length = configuration[offset];
        const uint8_t descriptor_type = configuration[offset + 1U];
        if (descriptor_length < 2U ||
            descriptor_length > total_length - offset)
            return false;

        if (descriptor_type == 4U) {
            if (descriptor_length < 9U ||
                !hid_finish_interface(&current, set)) return false;
            current = (struct hid_interface_scan){0};
            current.active = true;
            current.is_hid = configuration[offset + 5U] == 3U;
            current.item.number = configuration[offset + 2U];
            current.item.alternate_setting = configuration[offset + 3U];
            current.item.subclass = configuration[offset + 6U];
            current.item.protocol = configuration[offset + 7U];
        } else if (descriptor_type == 0x21U && current.active &&
                   current.is_hid) {
            if (descriptor_length < 6U) return false;
            const uint8_t descriptor_count = configuration[offset + 5U];
            if ((size_t)6U + (size_t)descriptor_count * 3U >
                descriptor_length) return false;
            for (uint8_t i = 0U; i < descriptor_count; ++i) {
                const size_t item = offset + 6U + (size_t)i * 3U;
                if (configuration[item] == 0x22U) {
                    current.item.report_descriptor_length = (uint16_t)(
                        configuration[item + 1U] |
                        ((uint16_t)configuration[item + 2U] << 8));
                    break;
                }
            }
        } else if (descriptor_type == 5U && current.active &&
                   current.is_hid) {
            if (descriptor_length < 7U) return false;
            const uint8_t address = configuration[offset + 2U];
            const uint8_t attributes = configuration[offset + 3U] & 0x03U;
            const uint16_t packet = (uint16_t)(
                configuration[offset + 4U] |
                ((uint16_t)(configuration[offset + 5U] & 0x07U) << 8));
            if ((address & 0x80U) != 0U && attributes == 3U &&
                packet != 0U && packet <= 1024U) {
                current.item.endpoint_in = address;
                current.item.max_packet = packet;
                current.item.interval = configuration[offset + 6U];
            }
        }
        offset += descriptor_length;
    }

    if (!hid_finish_interface(&current, set)) return false;
    return true;
}

bool nexus_usb_hid_find_companion_interface(
    const struct nexus_usb_hid_interface_set *set,
    uint8_t primary_interface, uint8_t wanted_boot_protocol,
    struct nexus_usb_hid_interface *companion) {
    if (set == NULL || companion == NULL ||
        (wanted_boot_protocol != 1U && wanted_boot_protocol != 2U) ||
        set->interface_count > NEXUS_USB_HID_MAX_INTERFACES) return false;

    for (uint8_t pass = 0U; pass < 2U; ++pass) {
        for (uint8_t i = 0U; i < set->interface_count; ++i) {
            const struct nexus_usb_hid_interface *candidate =
                &set->interfaces[i];
            if (candidate->number == primary_interface ||
                candidate->alternate_setting != 0U ||
                candidate->endpoint_in == 0U ||
                candidate->report_descriptor_length == 0U) continue;
            const bool boot = candidate->subclass == 1U &&
                candidate->protocol == wanted_boot_protocol;
            const bool generic = candidate->subclass != 1U ||
                (candidate->protocol != 1U && candidate->protocol != 2U);
            if ((pass == 0U && boot) || (pass == 1U && generic)) {
                *companion = *candidate;
                return true;
            }
        }
    }
    return false;
}
