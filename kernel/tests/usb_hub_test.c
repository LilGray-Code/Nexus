#include "usb_hub.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_hub_descriptor(void) {
    const uint8_t desc[] = {
        9U, 0x29U, 4U, 0x01U, 0x00U, 10U, 25U, 0U, 0U
    };
    struct nexus_usb_hub_descriptor hub = {0};
    assert(nexus_usb_hub_parse_descriptor(desc, sizeof(desc), false, &hub));
    assert(hub.port_count == 4U);
    assert(hub.individual_port_power);
    assert(hub.power_on_to_good_2ms == 10U);
    assert(hub.controller_current_ma == 25U);

    const uint8_t no_switch[] = {
        9U, 0x29U, 4U, 0x02U, 0x00U, 5U, 0U, 0U, 0U
    };
    assert(nexus_usb_hub_parse_descriptor(no_switch, sizeof(no_switch),
                                          true, &hub));
    assert(hub.no_power_switching && hub.multi_tt);
    assert(!nexus_usb_hub_parse_descriptor(no_switch, 6U, false, &hub));

    const uint8_t qemu_eight_port_short_bitmap[] = {
        10U, 0x29U, 8U, 0x00U, 0x00U, 50U, 0U, 0x00U, 0x00U, 0x00U
    };
    assert(nexus_usb_hub_parse_descriptor(
        qemu_eight_port_short_bitmap,
        sizeof(qemu_eight_port_short_bitmap), false, &hub));
    assert(hub.port_count == 8U);
    assert(hub.power_on_to_good_2ms == 50U);

    const uint8_t too_many[] = {
        9U, 0x29U, 16U, 1U, 0U, 5U, 0U, 0U, 0U
    };
    assert(!nexus_usb_hub_parse_descriptor(too_many, sizeof(too_many),
                                           false, &hub));
}

static void test_usb_route_strings(void) {
    struct nexus_usb_path root = {0}, child = {0}, grandchild = {0};
    assert(!nexus_usb_path_root(0U, &root));
    assert(nexus_usb_path_root(3U, &root));
    assert(root.root_port == 3U && root.depth == 0U &&
           root.route_string == 0U);

    assert(nexus_usb_path_child(&root, 7U, 2U, &child));
    assert(child.root_port == 3U && child.parent_hub_slot == 7U &&
           child.parent_port == 2U && child.depth == 1U);
    assert(child.route_string == 0x2U);

    assert(nexus_usb_path_child(&child, 9U, 5U, &grandchild));
    assert(grandchild.route_string == 0x52U);
    assert(grandchild.depth == 2U);

    /* Preserve a high-speed ancestor TT through a nested full-speed hub. */
    child.parent_hub_uses_tt = true;
    child.tt_hub_slot = 7U;
    child.tt_port = 2U;
    assert(nexus_usb_path_child(&child, 9U, 5U, &grandchild));
    assert(grandchild.parent_hub_uses_tt);
    assert(grandchild.tt_hub_slot == 7U && grandchild.tt_port == 2U);

    assert(!nexus_usb_path_child(&root, 0U, 1U, &child));
    assert(!nexus_usb_path_child(&root, 1U, 0U, &child));
    assert(!nexus_usb_path_child(&root, 1U, 16U, &child));
}

static void test_hub_port_status(void) {
    struct nexus_usb_hub_port_status port = {0};
    const uint8_t full_speed[] = {0x03U,0U,0x11U,0U};
    assert(nexus_usb_hub_decode_port_status(full_speed, &port));
    assert(port.connected && port.enabled && !port.resetting);
    assert(port.speed == NEXUS_USB_SPEED_FULL);
    assert(port.change_bits == 0x11U);

    const uint8_t low_speed[] = {0x01U,0x02U,0U,0U};
    assert(nexus_usb_hub_decode_port_status(low_speed, &port));
    assert(port.low_speed && port.speed == NEXUS_USB_SPEED_LOW);

    const uint8_t high_speed[] = {0x01U,0x04U,0U,0U};
    assert(nexus_usb_hub_decode_port_status(high_speed, &port));
    assert(port.high_speed && port.speed == NEXUS_USB_SPEED_HIGH);

    const uint8_t disconnected[] = {0U,0U,0U,0U};
    assert(nexus_usb_hub_decode_port_status(disconnected, &port));
    assert(!port.connected && !port.enabled);
    assert(port.speed == NEXUS_USB_SPEED_UNKNOWN);
}


static void test_superspeed_hub_descriptor(void) {
    const uint8_t desc[] = {
        11U,0x2AU,4U,0x01U,0x00U,10U,25U,3U,0x34U,0x12U,0U
    };
    struct nexus_usb_hub_descriptor hub = {0};
    assert(nexus_usb3_hub_parse_descriptor(desc, sizeof(desc), &hub));
    assert(hub.superspeed);
    assert(hub.port_count == 4U);
    assert(hub.header_decode_latency == 3U);
    assert(hub.hub_delay_ns == 0x1234U);
    assert(!nexus_usb3_hub_parse_descriptor(desc, 9U, &hub));

    const uint8_t wrong_type[] = {
        11U,0x29U,4U,0x01U,0x00U,10U,25U,3U,0x34U,0x12U,0U
    };
    assert(!nexus_usb3_hub_parse_descriptor(wrong_type,
        sizeof(wrong_type), &hub));
}

static void test_superspeed_hub_port_status(void) {
    struct nexus_usb_hub_port_status port = {0};
    const uint8_t super_speed[] = {0x03U,0x0CU,0x11U,0x00U};
    assert(nexus_usb3_hub_decode_port_status(super_speed, &port));
    assert(port.connected && port.enabled);
    assert(port.speed == NEXUS_USB_SPEED_SUPER);
    assert(port.change_bits == 0x0011U);

    const uint8_t super_plus[] = {0x01U,0x10U,0x00U,0x00U};
    assert(nexus_usb3_hub_decode_port_status(super_plus, &port));
    assert(port.connected && !port.enabled);
    assert(port.speed == NEXUS_USB_SPEED_SUPER_PLUS);

    const uint8_t invalid_speed[] = {0x01U,0x14U,0x00U,0x00U};
    assert(!nexus_usb3_hub_decode_port_status(invalid_speed, &port));
}

int main(void) {
    test_hub_descriptor();
    test_usb_route_strings();
    test_hub_port_status();
    test_superspeed_hub_descriptor();
    test_superspeed_hub_port_status();
    puts("NEXUS USB HUB DESCRIPTOR, PORT STATUS, AND ROUTE TESTS PASSED");
    return 0;
}
