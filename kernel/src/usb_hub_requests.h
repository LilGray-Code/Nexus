#ifndef NEXUS_USB_HUB_REQUESTS_H
#define NEXUS_USB_HUB_REQUESTS_H

#include <stdbool.h>
#include <stdint.h>

#define NEXUS_USB_HUB_GET_STATUS       0x00U
#define NEXUS_USB_HUB_CLEAR_FEATURE    0x01U
#define NEXUS_USB_HUB_SET_FEATURE      0x03U
#define NEXUS_USB_HUB_GET_DESCRIPTOR   0x06U

#define NEXUS_USB_HUB_FEATURE_PORT_RESET       4U
#define NEXUS_USB_HUB_FEATURE_PORT_POWER       8U
#define NEXUS_USB_HUB_FEATURE_C_PORT_CONNECTION 16U
#define NEXUS_USB_HUB_FEATURE_C_PORT_ENABLE     17U
#define NEXUS_USB_HUB_FEATURE_C_PORT_SUSPEND    18U
#define NEXUS_USB_HUB_FEATURE_C_PORT_OVER_CURRENT 19U
#define NEXUS_USB_HUB_FEATURE_C_PORT_RESET      20U

struct nexus_usb_setup_packet {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
};

bool nexus_usb_hub_get_descriptor_request(bool superspeed,
    struct nexus_usb_setup_packet *setup);
bool nexus_usb_hub_get_port_status_request(uint8_t port,
    struct nexus_usb_setup_packet *setup);
bool nexus_usb_hub_set_port_feature_request(uint8_t port, uint16_t feature,
    struct nexus_usb_setup_packet *setup);
bool nexus_usb_hub_clear_port_feature_request(uint8_t port, uint16_t feature,
    struct nexus_usb_setup_packet *setup);

#endif
