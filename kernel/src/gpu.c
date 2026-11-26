#include "gpu.h"

#include "pci.h"
#include "serial.h"

static struct nexus_gpu_info adapters[NEXUS_GPU_MAX_ADAPTERS];
static uint32_t adapter_count;
static bool firmware_framebuffer_active;
static bool virtio_display_active;
static struct nexus_gpu_runtime_capabilities runtime_capabilities;

static enum nexus_gpu_vendor identify_vendor(uint16_t vendor_id) {
    switch (vendor_id) {
        case 0x8086U: return NEXUS_GPU_VENDOR_INTEL;
        case 0x1002U: return NEXUS_GPU_VENDOR_AMD;
        case 0x10DEU: return NEXUS_GPU_VENDOR_NVIDIA;
        case 0x1AF4U: return NEXUS_GPU_VENDOR_VIRTIO;
        case 0x15ADU: return NEXUS_GPU_VENDOR_VMWARE;
        case 0x1234U: return NEXUS_GPU_VENDOR_QEMU;
        default: return NEXUS_GPU_VENDOR_UNKNOWN;
    }
}

static const char *vendor_name(enum nexus_gpu_vendor vendor) {
    switch (vendor) {
        case NEXUS_GPU_VENDOR_INTEL: return "INTEL GRAPHICS";
        case NEXUS_GPU_VENDOR_AMD: return "AMD RADEON";
        case NEXUS_GPU_VENDOR_NVIDIA: return "NVIDIA GRAPHICS";
        case NEXUS_GPU_VENDOR_VIRTIO: return "VIRTIO GPU";
        case NEXUS_GPU_VENDOR_VMWARE: return "VMWARE SVGA";
        case NEXUS_GPU_VENDOR_QEMU: return "QEMU DISPLAY";
        default: return "UNKNOWN DISPLAY";
    }
}

bool gpu_init(void) {
    adapter_count = 0U;
    firmware_framebuffer_active = false;
    virtio_display_active = false;
    runtime_capabilities = (struct nexus_gpu_runtime_capabilities){0};
    for (uint32_t i = 0U; i < NEXUS_GPU_MAX_ADAPTERS; ++i)
        adapters[i] = (struct nexus_gpu_info){0};

    /* Inventory every retained PCI display controller. Detection is not
     * proof of a usable driver; no BAR, MMIO register, or command queue is
     * touched by this generic discovery layer.
     */
    for (uint32_t i = 0U; i < pci_stored_device_count(); ++i) {
        const struct nexus_pci_device *device = pci_device_at(i);
        if (device == NULL || device->class_code != 0x03U ||
            adapter_count >= NEXUS_GPU_MAX_ADAPTERS) continue;

        struct nexus_gpu_info *info = &adapters[adapter_count++];
        info->present = true;
        info->vendor_id = device->vendor_id;
        info->device_id = device->device_id;
        info->subsystem_vendor_id = device->subsystem_vendor_id;
        info->subsystem_device_id = device->subsystem_device_id;
        info->revision_id = device->revision_id;
        info->class_code = device->class_code;
        info->subclass = device->subclass;
        info->programming_interface = device->programming_interface;
        info->pcie_capabilities_valid = device->extended_capabilities_valid;
        info->segment_group = device->segment_group;
        info->standard_capability_flags = device->standard_capability_flags;
        info->extended_capability_flags = device->extended_capability_flags;
        info->bus = device->bus;
        info->slot = device->slot;
        info->function = device->function;
        info->vendor = identify_vendor(device->vendor_id);
        info->name = vendor_name(info->vendor);
        info->hardware_2d_acceleration = false;
        info->hardware_3d_acceleration = false;
        info->firmware_framebuffer_active = false;
    }

    /* Prefer a conventional VGA-compatible display function when available;
     * otherwise the first enumerated display adapter remains primary.
     */
    if (adapter_count > 1U) {
        for (uint32_t i = 0U; i < adapter_count; ++i) {
            const struct nexus_pci_device *device = NULL;
            for (uint32_t j = 0U; j < pci_stored_device_count(); ++j) {
                const struct nexus_pci_device *candidate = pci_device_at(j);
                if (candidate != NULL &&
                    candidate->segment_group == adapters[i].segment_group &&
                    candidate->bus == adapters[i].bus &&
                    candidate->slot == adapters[i].slot &&
                    candidate->function == adapters[i].function) {
                    device = candidate;
                    break;
                }
            }
            if (device != NULL && device->subclass == 0x00U) {
                if (i != 0U) {
                    const struct nexus_gpu_info temporary = adapters[0];
                    adapters[0] = adapters[i];
                    adapters[i] = temporary;
                }
                break;
            }
        }
    }

    if (adapter_count != 0U) {
        const struct nexus_gpu_info *primary = &adapters[0];
        serial_write("GPU: primary display adapter detected: ");
        serial_write(primary->name);
        serial_write(" vendor/device=");
        serial_write_hex(primary->vendor_id);
        serial_write("/");
        serial_write_hex(primary->device_id);
        serial_write("\r\n");
        serial_write("GPU: total display adapters retained=");
        serial_write_hex(adapter_count);
        serial_write("\r\n");
    } else {
        serial_write("GPU: no PCI display controller recorded; keeping firmware framebuffer if supplied\r\n");
    }

    serial_write("GPU: acceleration is reported only after a device command engine is initialized\r\n");
    return adapter_count != 0U;
}

uint32_t gpu_adapter_count(void) {
    return adapter_count;
}

const struct nexus_gpu_info *gpu_adapter_at(uint32_t index) {
    return index < adapter_count ? &adapters[index] : NULL;
}

const struct nexus_gpu_info *gpu_primary_info(void) {
    return adapter_count != 0U ? &adapters[0] : NULL;
}

const char *gpu_primary_name(void) {
    return adapter_count != 0U ? adapters[0].name : "FIRMWARE DISPLAY";
}

const char *gpu_render_path_name(void) {
    if (virtio_display_active)
        return "GPU: VIRTIO HOST SCANOUT / CPU RASTER";
    return firmware_framebuffer_active
        ? "GPU: FIRMWARE SCANOUT / CPU RASTER"
        : "GPU: CPU RASTER / FRAMEBUFFER PENDING";
}

bool gpu_hardware_acceleration_available(void) {
    const struct nexus_gpu_info *primary = gpu_primary_info();
    return runtime_capabilities.hardware_2d_rendering ||
           runtime_capabilities.hardware_3d_rendering ||
           (primary != NULL &&
            (primary->hardware_2d_acceleration ||
             primary->hardware_3d_acceleration));
}

void gpu_set_firmware_framebuffer_active(bool active) {
    firmware_framebuffer_active = active;
    runtime_capabilities.firmware_scanout_active = active;
    if (active && !virtio_display_active)
        runtime_capabilities.virtio_scanout_count = 0U;
    if (adapter_count != 0U)
        adapters[0].firmware_framebuffer_active = active;
}

void gpu_set_virtio_display_active(bool active) {
    virtio_display_active = active;
    runtime_capabilities.virtio_scanout_active = active;
    if (!active) runtime_capabilities.virtio_scanout_count = 0U;
}

void gpu_set_virtio_display_outputs(uint32_t count) {
    runtime_capabilities.virtio_scanout_count =
        virtio_display_active ? count : 0U;
}

void gpu_get_runtime_capabilities(
    struct nexus_gpu_runtime_capabilities *capabilities) {
    if (capabilities == NULL) return;
    *capabilities = runtime_capabilities;
}
