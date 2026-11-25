#ifndef NEXUS_GPU_H
#define NEXUS_GPU_H

#include <stdbool.h>
#include <stdint.h>

#define NEXUS_GPU_MAX_ADAPTERS 16U

enum nexus_gpu_vendor {
    NEXUS_GPU_VENDOR_UNKNOWN = 0,
    NEXUS_GPU_VENDOR_INTEL,
    NEXUS_GPU_VENDOR_AMD,
    NEXUS_GPU_VENDOR_NVIDIA,
    NEXUS_GPU_VENDOR_VIRTIO,
    NEXUS_GPU_VENDOR_VMWARE,
    NEXUS_GPU_VENDOR_QEMU
};

struct nexus_gpu_info {
    bool present;
    bool firmware_framebuffer_active;
    bool hardware_2d_acceleration;
    bool hardware_3d_acceleration;
    enum nexus_gpu_vendor vendor;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_device_id;
    uint8_t revision_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    const char *name;
};

/* Read-only PCI discovery. This never writes a GPU BAR or command register. */
bool gpu_init(void);
uint32_t gpu_adapter_count(void);
const struct nexus_gpu_info *gpu_adapter_at(uint32_t index);
const struct nexus_gpu_info *gpu_primary_info(void);
const char *gpu_primary_name(void);
const char *gpu_render_path_name(void);
bool gpu_hardware_acceleration_available(void);
void gpu_set_firmware_framebuffer_active(bool active);
void gpu_set_virtio_display_active(bool active);

#endif
