#include "virtio_gpu_protocol.h"

#include <stdint.h>

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

bool nexus_virtio_gpu_decode_display_info(
    const uint8_t *response, size_t response_length,
    struct nexus_virtio_gpu_display_info *info) {
    if (response == NULL || info == NULL ||
        response_length < NEXUS_VIRTIO_GPU_DISPLAY_INFO_RESPONSE_SIZE ||
        read_le32(response) != NEXUS_VIRTIO_GPU_DISPLAY_INFO_RESPONSE)
        return false;

    struct nexus_virtio_gpu_display_info parsed = {0};
    for (uint32_t i = 0U; i < NEXUS_VIRTIO_GPU_MAX_SCANOUTS; ++i) {
        const size_t offset = 24U + (size_t)i * 24U;
        const uint32_t enabled = read_le32(response + offset + 16U);
        if (enabled > 1U) return false;
        if (enabled == 0U) continue;

        struct nexus_virtio_gpu_scanout *scanout = &parsed.scanouts[i];
        scanout->x = read_le32(response + offset);
        scanout->y = read_le32(response + offset + 4U);
        scanout->width = read_le32(response + offset + 8U);
        scanout->height = read_le32(response + offset + 12U);
        if (scanout->width == 0U || scanout->height == 0U ||
            scanout->x > UINT32_MAX - scanout->width ||
            scanout->y > UINT32_MAX - scanout->height)
            return false;
        scanout->enabled = true;
        ++parsed.enabled_scanout_count;
    }

    *info = parsed;
    return true;
}
