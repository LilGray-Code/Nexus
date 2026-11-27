#include "usb_host_controller.h"

enum nexus_usb_host_controller_kind nexus_usb_host_controller_classify(
    uint8_t class_code, uint8_t subclass, uint8_t programming_interface) {
    if (class_code != 0x0CU || subclass != 0x03U)
        return NEXUS_USB_HOST_UNKNOWN;
    switch (programming_interface) {
        case 0x00U: return NEXUS_USB_HOST_UHCI;
        case 0x10U: return NEXUS_USB_HOST_OHCI;
        case 0x20U: return NEXUS_USB_HOST_EHCI;
        case 0x30U: return NEXUS_USB_HOST_XHCI;
        default: return NEXUS_USB_HOST_UNKNOWN;
    }
}

bool nexus_usb_host_controller_driver_available(
    enum nexus_usb_host_controller_kind kind) {
    return kind == NEXUS_USB_HOST_XHCI;
}

const char *nexus_usb_host_controller_name(
    enum nexus_usb_host_controller_kind kind) {
    switch (kind) {
        case NEXUS_USB_HOST_UHCI: return "UHCI";
        case NEXUS_USB_HOST_OHCI: return "OHCI";
        case NEXUS_USB_HOST_EHCI: return "EHCI";
        case NEXUS_USB_HOST_XHCI: return "xHCI";
        default: return "unknown";
    }
}
