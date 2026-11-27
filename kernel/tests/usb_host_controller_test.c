#include "usb_host_controller.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(nexus_usb_host_controller_classify(0x0CU,0x03U,0x00U) ==
           NEXUS_USB_HOST_UHCI);
    assert(nexus_usb_host_controller_classify(0x0CU,0x03U,0x10U) ==
           NEXUS_USB_HOST_OHCI);
    assert(nexus_usb_host_controller_classify(0x0CU,0x03U,0x20U) ==
           NEXUS_USB_HOST_EHCI);
    assert(nexus_usb_host_controller_classify(0x0CU,0x03U,0x30U) ==
           NEXUS_USB_HOST_XHCI);
    assert(nexus_usb_host_controller_classify(0x02U,0x03U,0x30U) ==
           NEXUS_USB_HOST_UNKNOWN);
    assert(nexus_usb_host_controller_classify(0x0CU,0x04U,0x20U) ==
           NEXUS_USB_HOST_UNKNOWN);
    assert(nexus_usb_host_controller_driver_available(NEXUS_USB_HOST_XHCI));
    assert(!nexus_usb_host_controller_driver_available(NEXUS_USB_HOST_EHCI));
    assert(!nexus_usb_host_controller_driver_available(NEXUS_USB_HOST_UHCI));
    assert(!nexus_usb_host_controller_driver_available(NEXUS_USB_HOST_OHCI));
    puts("NEXUS USB UHCI/OHCI/EHCI/xHCI CONTROLLER IDENTIFICATION TESTS PASSED");
    return 0;
}
