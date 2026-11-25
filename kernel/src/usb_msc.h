#ifndef NEXUS_USB_MSC_H
#define NEXUS_USB_MSC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_USB_MSC_MAX_TRANSFER_BYTES 4096U
#define NEXUS_USB_MSC_SENSE_BYTES 18U

/* Bulk-Only Transport is kept separate from the xHCI transport. The xHCI
 * driver supplies exact bulk endpoint transfers and BOT reset recovery.
 */
struct nexus_usb_msc_transport {
    bool (*bulk_out)(void *context, uint8_t endpoint,
                     const void *buffer, size_t length, size_t *actual);
    bool (*bulk_in)(void *context, uint8_t endpoint,
                    void *buffer, size_t capacity, size_t *actual);
    /* Issue class-specific Mass Storage Reset, then clear both endpoint halts. */
    bool (*reset_recovery)(void *context, uint8_t interface_number,
                           uint8_t bulk_in_endpoint, uint8_t bulk_out_endpoint);
};

struct nexus_usb_msc_sense {
    uint8_t key;
    uint8_t additional_code;
    uint8_t additional_code_qualifier;
};

struct nexus_usb_msc_interface {
    uint8_t configuration_value;
    uint8_t interface_number;
    uint8_t bulk_in_endpoint;
    uint8_t bulk_out_endpoint;
    uint16_t bulk_in_max_packet;
    uint16_t bulk_out_max_packet;
};

struct nexus_usb_msc_device {
    struct nexus_usb_msc_transport transport;
    void *transport_context;
    uint8_t interface_number;
    uint8_t bulk_in_endpoint;
    uint8_t bulk_out_endpoint;
    uint8_t lun;
    uint32_t next_tag;
    uint32_t block_size;
    uint32_t max_blocks_per_request;
    uint64_t block_count;
    char vendor[9];
    char product[17];
    char revision[5];
    uint8_t last_csw_status;
    bool initialized;
};

/* Finds the SCSI-transparent Bulk-Only Transport interface in a standard
 * USB configuration descriptor. The selected alternate setting must be zero
 * and must expose one bulk-IN and one bulk-OUT endpoint.
 */
bool nexus_usb_msc_find_interface(
    const uint8_t *configuration, size_t length,
    struct nexus_usb_msc_interface *interface_info);

/* Initializes a directly attached BOT mass-storage LUN. It validates INQUIRY,
 * TEST UNIT READY, and READ CAPACITY (10/16) before reporting it usable.
 * The USB bus driver owns enumeration, endpoint setup, and DMA safety.
 */
bool nexus_usb_msc_init(struct nexus_usb_msc_device *device,
                        const struct nexus_usb_msc_transport *transport,
                        void *transport_context,
                        uint8_t interface_number,
                        uint8_t bulk_in_endpoint,
                        uint8_t bulk_out_endpoint,
                        uint8_t lun);

/* Low-level SCSI commands. Disk writes must be routed through the kernel's
 * blockdev write/session policy; callers must never bypass that policy.
 */
bool nexus_usb_msc_read_blocks(struct nexus_usb_msc_device *device,
                               uint64_t lba, uint32_t block_count,
                               void *buffer);
bool nexus_usb_msc_write_blocks(struct nexus_usb_msc_device *device,
                                uint64_t lba, uint32_t block_count,
                                const void *buffer);
bool nexus_usb_msc_flush(struct nexus_usb_msc_device *device);
bool nexus_usb_msc_request_sense(struct nexus_usb_msc_device *device,
                                 struct nexus_usb_msc_sense *sense);
bool nexus_usb_msc_ready(const struct nexus_usb_msc_device *device);

#endif
