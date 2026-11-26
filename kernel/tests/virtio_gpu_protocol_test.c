#include "virtio_gpu_protocol.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint8_t response[NEXUS_VIRTIO_GPU_DISPLAY_INFO_RESPONSE_SIZE];

static void reset_response(void) {
    memset(response, 0, sizeof(response));
    write_le32(response, NEXUS_VIRTIO_GPU_DISPLAY_INFO_RESPONSE);
}

static void test_decode_single_and_multiple_scanouts(void) {
    reset_response();
    write_le32(response + 24U, 0U);
    write_le32(response + 28U, 0U);
    write_le32(response + 32U, 1920U);
    write_le32(response + 36U, 1080U);
    write_le32(response + 40U, 1U);
    write_le32(response + 24U + 24U, 1920U);
    write_le32(response + 28U + 24U, 0U);
    write_le32(response + 32U + 24U, 2560U);
    write_le32(response + 36U + 24U, 1440U);
    write_le32(response + 40U + 24U, 1U);

    struct nexus_virtio_gpu_display_info info;
    memset(&info, 0xA5, sizeof(info));
    assert(nexus_virtio_gpu_decode_display_info(
        response, sizeof(response), &info));
    assert(info.enabled_scanout_count == 2U);
    assert(info.scanouts[0].enabled);
    assert(info.scanouts[0].width == 1920U);
    assert(info.scanouts[0].height == 1080U);
    assert(info.scanouts[1].enabled);
    assert(info.scanouts[1].x == 1920U);
    assert(info.scanouts[1].width == 2560U);
    assert(!info.scanouts[2].enabled);
}

static void test_reject_invalid_response_and_bounds(void) {
    struct nexus_virtio_gpu_display_info info;
    reset_response();
    assert(!nexus_virtio_gpu_decode_display_info(
        response, sizeof(response) - 1U, &info));
    write_le32(response, 0x1100U);
    assert(!nexus_virtio_gpu_decode_display_info(
        response, sizeof(response), &info));

    reset_response();
    write_le32(response + 40U, 2U);
    assert(!nexus_virtio_gpu_decode_display_info(
        response, sizeof(response), &info));

    reset_response();
    write_le32(response + 24U + 8U, 0xFFFFFFFFU);
    write_le32(response + 24U + 16U, 1U);
    assert(!nexus_virtio_gpu_decode_display_info(
        response, sizeof(response), &info));
}

static void test_empty_and_invalid_arguments(void) {
    reset_response();
    struct nexus_virtio_gpu_display_info info;
    assert(nexus_virtio_gpu_decode_display_info(
        response, sizeof(response), &info));
    assert(info.enabled_scanout_count == 0U);
    assert(!nexus_virtio_gpu_decode_display_info(
        NULL, sizeof(response), &info));
    assert(!nexus_virtio_gpu_decode_display_info(
        response, sizeof(response), NULL));
}

int main(void) {
    test_decode_single_and_multiple_scanouts();
    test_reject_invalid_response_and_bounds();
    test_empty_and_invalid_arguments();
    puts("NEXUS VIRTIO GPU DISPLAY INFO TESTS PASSED");
    return 0;
}
