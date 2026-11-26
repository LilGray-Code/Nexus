#ifndef NEXUS_GPU_H
#define NEXUS_GPU_H

#include <stdbool.h>
#include <stdint.h>

#define NEXUS_GPU_MAX_ADAPTERS 16U

enum nexus_gpu_family {
    NEXUS_GPU_FAMILY_UNKNOWN = 0,
    NEXUS_GPU_FAMILY_INTEL_GENERIC,
    NEXUS_GPU_FAMILY_INTEL_XE_LPG,
    NEXUS_GPU_FAMILY_INTEL_ARC_ALCHEMIST,
    NEXUS_GPU_FAMILY_INTEL_ARC_BATTLEMAGE,
    NEXUS_GPU_FAMILY_INTEL_XE2,
    NEXUS_GPU_FAMILY_INTEL_XE3,
    NEXUS_GPU_FAMILY_AMD_RADEON,
    NEXUS_GPU_FAMILY_NVIDIA,
    NEXUS_GPU_FAMILY_VIRTIO,
    NEXUS_GPU_FAMILY_VMWARE,
    NEXUS_GPU_FAMILY_QEMU
};

struct nexus_gpu_bar_info {
    bool present;
    bool assigned;
    bool io_space;
    bool is_64bit;
    bool prefetchable;
    uint64_t base;
};

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
    enum nexus_gpu_family family;
    const char *family_name;
    uint8_t bar_count;
    uint8_t memory_bar_count;
    bool has_64bit_memory_bar;
    bool has_prefetchable_memory_bar;
    struct nexus_gpu_bar_info bars[NEXUS_GPU_MAX_ADAPTERS > 0U ? 6U : 6U];
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_device_id;
    uint8_t revision_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    bool pcie_capabilities_valid;
    uint16_t segment_group;
    uint32_t standard_capability_flags;
    uint32_t extended_capability_flags;
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
