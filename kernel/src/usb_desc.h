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


#define NEXUS_USB_INVENTORY_MAX_DEVICES 32U

enum nexus_usb_network_interface_kind {
    NEXUS_USB_NETWORK_NONE = 0,
    NEXUS_USB_NETWORK_CDC_ECM,
    NEXUS_USB_NETWORK_CDC_NCM,
    NEXUS_USB_NETWORK_MBIM,
    NEXUS_USB_NETWORK_RNDIS
};

struct nexus_usb_device_info {
    uint8_t port;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_revision;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t network_interface_number;
    uint8_t network_interface_class;
    uint8_t network_interface_subclass;
    uint8_t network_interface_protocol;
    enum nexus_usb_network_interface_kind network_kind;
    bool has_vendor_specific_interface;
};

/* Host-testable USB identity parsing and a bounded boot-time inventory.
 * Recording a VID/PID is identification only: it does not bind a driver.
 */
bool nexus_usb_parse_device_descriptor(
    uint8_t port, const uint8_t *descriptor, size_t length,
    struct nexus_usb_device_info *result);
void nexus_usb_inventory_reset(void);
bool nexus_usb_inventory_add_device(
    uint8_t port, const uint8_t *descriptor, size_t length);
bool nexus_usb_inventory_set_configuration(
    uint8_t port, const uint8_t *configuration, size_t length);
size_t nexus_usb_inventory_count(void);
bool nexus_usb_inventory_get(size_t index,
                             struct nexus_usb_device_info *result);

#endif
