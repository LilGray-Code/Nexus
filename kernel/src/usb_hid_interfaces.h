#ifndef NEXUS_USB_HID_INTERFACES_H
#define NEXUS_USB_HID_INTERFACES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_USB_HID_MAX_INTERFACES 8U

struct nexus_usb_hid_interface {
    uint8_t number;
    uint8_t alternate_setting;
    uint8_t subclass;
    uint8_t protocol;
    uint8_t endpoint_in;
    uint16_t max_packet;
    uint8_t interval;
    uint16_t report_descriptor_length;
};

struct nexus_usb_hid_interface_set {
    uint8_t configuration_value;
    uint8_t interface_count;
    struct nexus_usb_hid_interface interfaces[NEXUS_USB_HID_MAX_INTERFACES];
};

/* Collect every HID interface with an interrupt-IN endpoint from one bounded
 * USB configuration descriptor. This identifies composite receivers without
 * assuming that one USB device corresponds to one HID interface. */
bool nexus_usb_hid_collect_interfaces(
    const uint8_t *configuration, size_t length,
    struct nexus_usb_hid_interface_set *set);

#endif
