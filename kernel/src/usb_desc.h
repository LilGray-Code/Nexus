#ifndef NEXUS_USB_DESC_H
#define NEXUS_USB_DESC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_USB_DESC_ANY 0xFFU

struct nexus_usb_bulk_interface {
    uint8_t configuration_value;
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t bulk_in_endpoint;
    uint8_t bulk_out_endpoint;
    uint16_t bulk_in_max_packet;
    uint16_t bulk_out_max_packet;
};

/*
 * Find one interface with a matched bulk IN/OUT endpoint pair.
 * Class fields accept NEXUS_USB_DESC_ANY as a wildcard. Only endpoints
 * belonging to the selected interface and alternate setting are paired.
 */
bool nexus_usb_find_bulk_interface(
    const uint8_t *configuration, size_t length,
    uint8_t class_code, uint8_t subclass, uint8_t protocol,
    struct nexus_usb_bulk_interface *result);

#endif
