#include "input.h"
#include "serial.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

void serial_write(const char *text) { (void)text; }
void serial_write_hex(uint64_t value) { (void)value; }
void pic_irq_unmask(uint8_t irq) { (void)irq; }

static void test_gamepad_queue_preserves_controller_state(void) {
    struct nexus_usb_gamepad_event sent = {
        .device_id = 7U,
        .vendor_id = 0x045EU,
        .product_id = 0x028EU,
        .buttons = 0xA501U,
        .axis_count = 4U,
        .hat = 3U,
        .axes = {-32767, 1234, 32767, -2222}
    };
    struct nexus_usb_gamepad_event received = {0};
    input_usb_gamepad_event(&sent);
    assert(input_pop_gamepad_event(&received));
    assert(received.device_id == sent.device_id);
    assert(received.vendor_id == sent.vendor_id);
    assert(received.product_id == sent.product_id);
    assert(received.buttons == sent.buttons);
    assert(received.axis_count == sent.axis_count);
    assert(received.hat == sent.hat);
    for (size_t i = 0U; i < NEXUS_USB_GAMEPAD_MAX_AXES; ++i)
        assert(received.axes[i] == sent.axes[i]);
    assert(!input_pop_gamepad_event(&received));
}

static void test_gamepad_queue_is_bounded_and_wraps(void) {
    struct nexus_usb_gamepad_event sent = {0};
    struct nexus_usb_gamepad_event received = {0};
    for (uint16_t i = 1U; i <= 15U; ++i) {
        sent.device_id = i;
        sent.buttons = 1U << (i % 16U);
        input_usb_gamepad_event(&sent);
    }
    /* The sixteenth write is dropped instead of overwriting unread events. */
    sent.device_id = 99U;
    input_usb_gamepad_event(&sent);
    for (uint16_t i = 1U; i <= 15U; ++i) {
        assert(input_pop_gamepad_event(&received));
        assert(received.device_id == i);
    }
    assert(!input_pop_gamepad_event(&received));
    sent.device_id = 100U;
    input_usb_gamepad_event(&sent);
    assert(input_pop_gamepad_event(&received));
    assert(received.device_id == 100U);
}

int main(void) {
    test_gamepad_queue_preserves_controller_state();
    test_gamepad_queue_is_bounded_and_wraps();
    puts("NEXUS GAMEPAD INPUT QUEUE TESTS PASSED");
    return 0;
}
