#ifndef NEXUS_USB_HUB_H
#define NEXUS_USB_HUB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_USB2_HUB_DESCRIPTOR_TYPE 0x29U
#define NEXUS_USB3_HUB_DESCRIPTOR_TYPE 0x2AU
#define NEXUS_USB_HUB_MAX_PORTS 15U
#define NEXUS_USB_ROUTE_MAX_DEPTH 5U

/* Values deliberately match xHCI PORTSC speed IDs so a child port's
 * decoded speed can be written directly into its Slot Context. */
enum nexus_usb_speed {
    NEXUS_USB_SPEED_UNKNOWN = 0,
    NEXUS_USB_SPEED_FULL = 1,
    NEXUS_USB_SPEED_LOW = 2,
    NEXUS_USB_SPEED_HIGH = 3,
    NEXUS_USB_SPEED_SUPER = 4,
    NEXUS_USB_SPEED_SUPER_PLUS = 5
};

struct nexus_usb_hub_descriptor {
    uint8_t port_count;
    uint16_t characteristics;
    uint8_t power_on_to_good_2ms;
    uint8_t controller_current_ma;
    bool individual_port_power;
    bool no_power_switching;
    bool multi_tt;
    bool superspeed;
    uint8_t header_decode_latency;
    uint16_t hub_delay_ns;
};

struct nexus_usb_path {
    uint8_t root_port;
    uint8_t parent_hub_slot;
    uint8_t parent_port;
    uint8_t depth;
    uint32_t route_string;
    bool parent_hub_uses_tt;
    /* Nearest upstream high-speed USB2 hub that provides the transaction
     * translator for a low/full-speed child. Propagated through nested hubs. */
    uint8_t tt_hub_slot;
    uint8_t tt_port;
};

struct nexus_usb_hub_port_status {
    bool connected;
    bool enabled;
    bool resetting;
    bool low_speed;
    bool high_speed;
    uint16_t change_bits;
    enum nexus_usb_speed speed;
};

struct nexus_usb_hub_interrupt_endpoint {
    uint8_t address;
    uint16_t max_packet;
    uint8_t interval;
};

/* Parse the hub interrupt-IN endpoint from the active configuration. The
 * minimum packet size must cover bit zero plus one change bit per port. */
bool nexus_usb_hub_find_interrupt_endpoint(
    const uint8_t *configuration, size_t length, size_t minimum_packet_bytes,
    struct nexus_usb_hub_interrupt_endpoint *endpoint);

/* Decode the USB hub interrupt bitmap: bit zero is hub status, bit N is port N.
 * changed_ports uses bit N-1 for downstream port N (up to 15 ports). */
bool nexus_usb_hub_decode_change_bitmap(
    const uint8_t *bitmap, size_t bitmap_length, uint8_t port_count,
    uint16_t *changed_ports, bool *hub_status_changed);

bool nexus_usb_hub_parse_descriptor(
    const uint8_t *descriptor, size_t length, bool multi_tt,
    struct nexus_usb_hub_descriptor *hub);

/* SuperSpeed hub descriptor (USB 3.x, descriptor type 0x2A). */
bool nexus_usb3_hub_parse_descriptor(
    const uint8_t *descriptor, size_t length,
    struct nexus_usb_hub_descriptor *hub);

bool nexus_usb_path_root(uint8_t root_port, struct nexus_usb_path *path);
bool nexus_usb_path_child(const struct nexus_usb_path *parent,
                          uint8_t hub_slot, uint8_t child_port,
                          struct nexus_usb_path *child);
bool nexus_usb_hub_decode_port_status(
    const uint8_t status[4], struct nexus_usb_hub_port_status *decoded);

/* SuperSpeed PORT_STATUS/PORT_CHANGE are a 32-bit little-endian value. */
bool nexus_usb3_hub_decode_port_status(
    const uint8_t status[4], struct nexus_usb_hub_port_status *decoded);

#endif
