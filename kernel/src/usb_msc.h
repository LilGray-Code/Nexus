#ifndef NEXUS_USB_MSC_H
#define NEXUS_USB_MSC_H

#include "blockdev.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* xHCI supplies synchronous Bulk-Only Transport endpoints. Each callback
 * must report the exact number of bytes completed; short BOT phases fail.
 */
struct nexus_usb_msc_transport {
    void *context;
    bool (*bulk_in)(void *context, void *buffer, size_t length,
                    size_t *transferred);
    bool (*bulk_out)(void *context, const void *buffer, size_t length,
                     size_t *transferred);
    bool (*reset)(void *context);
    char device_name[NEXUS_BLOCKDEV_NAME_MAX];
    char stable_id[NEXUS_BLOCKDEV_ID_MAX];
};

bool nexus_usb_msc_attach(const struct nexus_usb_msc_transport *transport);
bool nexus_usb_msc_ready(void);
const struct nexus_block_device *nexus_usb_msc_block_device(void);

#endif
