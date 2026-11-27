#ifndef NEXUS_BLUETOOTH_USB_H
#define NEXUS_BLUETOOTH_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* USB Bluetooth class interface: Wireless Controller / RF / Bluetooth. */
struct nexus_bluetooth_usb_interface {
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t interrupt_in;
    uint16_t interrupt_in_max_packet;
    uint8_t interrupt_interval;
    uint8_t bulk_in;
    uint16_t bulk_in_max_packet;
    uint8_t bulk_out;
    uint16_t bulk_out_max_packet;
};

bool nexus_bluetooth_usb_find_interface(
    const uint8_t *configuration, size_t length,
    struct nexus_bluetooth_usb_interface *interface);

#endif
