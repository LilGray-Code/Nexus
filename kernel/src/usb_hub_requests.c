#include "usb_hub_requests.h"

#include <stddef.h>

static bool port_request(uint8_t port, uint16_t feature, uint8_t request,
                         struct nexus_usb_setup_packet *setup) {
    if (port == 0U || setup == NULL) return false;
    *setup = (struct nexus_usb_setup_packet){
        .request_type = 0x23U, /* host-to-device, class, other/port */
        .request = request,
        .value = feature,
        .index = port,
        .length = 0U
    };
    return true;
}

bool nexus_usb_hub_get_descriptor_request(
    bool superspeed, struct nexus_usb_setup_packet *setup) {
    if (setup == NULL) return false;
    *setup = (struct nexus_usb_setup_packet){
        .request_type = 0xA0U, /* device-to-host, class, device */
        .request = NEXUS_USB_HUB_GET_DESCRIPTOR,
        .value = (uint16_t)((superspeed ? 0x2AU : 0x29U) << 8),
        .index = 0U,
        .length = 0U
    };
    return true;
}

bool nexus_usb_hub_get_port_status_request(
    uint8_t port, struct nexus_usb_setup_packet *setup) {
    if (port == 0U || setup == NULL) return false;
    *setup = (struct nexus_usb_setup_packet){
        .request_type = 0xA3U, /* device-to-host, class, other/port */
        .request = NEXUS_USB_HUB_GET_STATUS,
        .value = 0U,
        .index = port,
        .length = 4U
    };
    return true;
}

bool nexus_usb_hub_set_port_feature_request(
    uint8_t port, uint16_t feature, struct nexus_usb_setup_packet *setup) {
    return port_request(port, feature, NEXUS_USB_HUB_SET_FEATURE, setup);
}

bool nexus_usb_hub_clear_port_feature_request(
    uint8_t port, uint16_t feature, struct nexus_usb_setup_packet *setup) {
    return port_request(port, feature, NEXUS_USB_HUB_CLEAR_FEATURE, setup);
}
