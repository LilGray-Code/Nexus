#ifndef NEXUS_VIRTIO_GPU_H
#define NEXUS_VIRTIO_GPU_H

#include "graphics.h"
#include "virtio_gpu_protocol.h"
#include "display_edid.h"

#include <stdbool.h>

/*
 * Start a VirtIO-GPU 2D scanout and return a CPU-writable backing surface.
 * The CPU rasterizer draws to that resource; present uploads modified rectangles through TRANSFER_TO_HOST_2D and
 * RESOURCE_FLUSH.
 */
bool virtio_gpu_start(uint64_t width, uint64_t height,
                      const struct nexus_graphics_surface *firmware_surface,
                      struct nexus_graphics_surface *render_surface);
bool virtio_gpu_present(void);
bool virtio_gpu_present_region(uint64_t x, uint64_t y,
                               uint64_t width, uint64_t height);
bool virtio_gpu_active(void);
/* Scanout indexes match the VirtIO GPU scanout IDs (0 through 15). */
uint32_t virtio_gpu_scanout_count(void);
bool virtio_gpu_scanout_info(
    uint32_t scanout_id, struct nexus_virtio_gpu_scanout *info);
/* Returns EDID-derived base-block timings when the device advertises EDID. */
bool virtio_gpu_edid_info(
    uint32_t scanout_id, struct nexus_edid_info *info);
void virtio_gpu_stop(void);

#endif
