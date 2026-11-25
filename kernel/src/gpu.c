#include "gpu.h"

#include "pci.h"
#include "serial.h"

static struct nexus_gpu_info primary_gpu;
static uint32_t detected_gpu_count;

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
    primary_gpu = (struct nexus_gpu_info){0};
    detected_gpu_count = 0U;

    /* PCI class 03 is a display controller. Firmware remains responsible for
     * the active scanout mode until a reviewed device driver is available.
     */
    for (uint32_t i = 0U; i < pci_stored_device_count(); ++i) {
        const struct nexus_pci_device *device = pci_device_at(i);
        if (device == NULL || device->class_code != 0x03U) continue;

        ++detected_gpu_count;
        if (!primary_gpu.present || device->subclass == 0x00U) {
            primary_gpu.present = true;
            primary_gpu.vendor_id = device->vendor_id;
            primary_gpu.device_id = device->device_id;
            primary_gpu.bus = device->bus;
            primary_gpu.slot = device->slot;
            primary_gpu.function = device->function;
            primary_gpu.vendor = identify_vendor(device->vendor_id);
            primary_gpu.name = vendor_name(primary_gpu.vendor);
        }
        if (device->subclass == 0x00U) break;
    }

    if (primary_gpu.present) {
        serial_write("GPU: display adapter detected: ");
        serial_write(primary_gpu.name);
        serial_write(" vendor/device=");
        serial_write_hex(primary_gpu.vendor_id);
        serial_write("/");
        serial_write_hex(primary_gpu.device_id);
        serial_write("\r\n");
    } else {
        serial_write("GPU: no PCI display controller recorded; keeping firmware framebuffer if supplied\r\n");
    }

    /* Detection is not a driver. No 2D/3D command engine is enabled here. */
    primary_gpu.hardware_2d_acceleration = false;
    primary_gpu.hardware_3d_acceleration = false;
    serial_write("GPU: firmware framebuffer scanout + Nexus CPU renderer; GPU command acceleration unavailable\r\n");
    return primary_gpu.present;
}

const struct nexus_gpu_info *gpu_primary_info(void) {
    return &primary_gpu;
}

const char *gpu_primary_name(void) {
    return primary_gpu.present ? primary_gpu.name : "FIRMWARE DISPLAY";
}

const char *gpu_render_path_name(void) {
    return primary_gpu.firmware_framebuffer_active
        ? "GPU SCANOUT / CPU RENDERER"
        : "CPU RENDERER / FRAMEBUFFER PENDING";
}

bool gpu_hardware_acceleration_available(void) {
    return primary_gpu.hardware_2d_acceleration ||
           primary_gpu.hardware_3d_acceleration;
}

void gpu_set_firmware_framebuffer_active(bool active) {
    primary_gpu.firmware_framebuffer_active = active;
}

