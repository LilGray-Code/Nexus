#ifndef NEXUS_VIRTIO_GPU_PROTOCOL_H
#define NEXUS_VIRTIO_GPU_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_VIRTIO_GPU_MAX_SCANOUTS 16U
#define NEXUS_VIRTIO_GPU_DISPLAY_INFO_RESPONSE 0x1101U
#define NEXUS_VIRTIO_GPU_DISPLAY_INFO_RESPONSE_SIZE (24U + 24U * NEXUS_VIRTIO_GPU_MAX_SCANOUTS)

struct nexus_virtio_gpu_scanout {
    bool enabled;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct nexus_virtio_gpu_display_info {
    uint32_t enabled_scanout_count;
    struct nexus_virtio_gpu_scanout scanouts[NEXUS_VIRTIO_GPU_MAX_SCANOUTS];
};

/* Decode the little-endian VIRTIO_GPU_RESP_OK_DISPLAY_INFO wire response. */
bool nexus_virtio_gpu_decode_display_info(
    const uint8_t *response, size_t response_length,
    struct nexus_virtio_gpu_display_info *info);

#endif
