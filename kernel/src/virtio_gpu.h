#ifndef NEXUS_VIRTIO_GPU_H
#define NEXUS_VIRTIO_GPU_H

#include "graphics.h"

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
void virtio_gpu_stop(void);

#endif
