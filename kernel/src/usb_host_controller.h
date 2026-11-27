#ifndef NEXUS_USB_HOST_CONTROLLER_H
#define NEXUS_USB_HOST_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

enum nexus_usb_host_controller_kind {
    NEXUS_USB_HOST_UNKNOWN = 0,
    NEXUS_USB_HOST_UHCI,
    NEXUS_USB_HOST_OHCI,
    NEXUS_USB_HOST_EHCI,
    NEXUS_USB_HOST_XHCI
};

enum nexus_usb_host_controller_kind nexus_usb_host_controller_classify(
    uint8_t class_code, uint8_t subclass, uint8_t programming_interface);

/* Only xHCI has an operational host-controller driver in this kernel build. */
bool nexus_usb_host_controller_driver_available(
    enum nexus_usb_host_controller_kind kind);
const char *nexus_usb_host_controller_name(
    enum nexus_usb_host_controller_kind kind);

#endif
