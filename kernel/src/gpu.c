#include "gpu.h"

#include "pci.h"
#include "serial.h"

static struct nexus_gpu_info adapters[NEXUS_GPU_MAX_ADAPTERS];
static uint32_t adapter_count;
static bool firmware_framebuffer_active;
static bool virtio_display_active;

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

static bool device_id_in_list(uint16_t device_id,
                               const uint16_t *ids, uint32_t count) {
    if (ids == NULL) return false;
    for (uint32_t i = 0U; i < count; ++i)
        if (ids[i] == device_id) return true;
    return false;
}

/*
 * These are identification tables, not driver match tables. An architecture
 * label helps diagnostics choose a future driver, but does not imply that a
 * command processor, memory manager, or display engine is supported.
 *
 * Intel Arc and Xe IDs are kept to explicitly catalogued upstream/vendor
 * hardware IDs. Unknown Intel devices remain visible as generic Intel GPUs.
 */
static enum nexus_gpu_family identify_family(uint16_t vendor_id,
                                               uint16_t device_id) {
    static const uint16_t intel_xe_lpg_ids[] = {
        0x7D40U, 0x7D41U, 0x7D45U, 0x7D51U, 0x7D55U, 0x7D67U, 0x7DD5U
    };
    static const uint16_t intel_arc_alchemist_ids[] = {
        0x5690U, 0x5691U, 0x5692U, 0x5693U, 0x5694U,
        0x5696U, 0x5697U, 0x56A0U, 0x56A1U, 0x56A2U,
        0x56A5U, 0x56A6U, 0x56B0U, 0x56B1U, 0x56B2U,
        0x56B3U, 0x56BAU, 0x56BCU, 0x56BDU
    };
    static const uint16_t intel_arc_battlemage_ids[] = {
        0xE20BU, 0xE20CU, 0xE211U, 0xE212U, 0xE222U, 0xE223U
    };
    static const uint16_t intel_xe2_ids[] = { 0x64A0U };
    static const uint16_t intel_xe3_ids[] = {
        0xB080U, 0xB081U, 0xB082U, 0xB083U, 0xB084U,
        0xB085U, 0xB086U, 0xB087U, 0xB090U, 0xB0A0U
    };

    switch (vendor_id) {
        case 0x8086U:
            if (device_id_in_list(device_id, intel_arc_alchemist_ids,
                                  sizeof(intel_arc_alchemist_ids) /
                                  sizeof(intel_arc_alchemist_ids[0])))
                return NEXUS_GPU_FAMILY_INTEL_ARC_ALCHEMIST;
            if (device_id_in_list(device_id, intel_arc_battlemage_ids,
                                  sizeof(intel_arc_battlemage_ids) /
                                  sizeof(intel_arc_battlemage_ids[0])))
                return NEXUS_GPU_FAMILY_INTEL_ARC_BATTLEMAGE;
            if (device_id_in_list(device_id, intel_xe3_ids,
                                  sizeof(intel_xe3_ids) /
                                  sizeof(intel_xe3_ids[0])))
                return NEXUS_GPU_FAMILY_INTEL_XE3;
            if (device_id_in_list(device_id, intel_xe2_ids,
                                  sizeof(intel_xe2_ids) /
                                  sizeof(intel_xe2_ids[0])))
                return NEXUS_GPU_FAMILY_INTEL_XE2;
            if (device_id_in_list(device_id, intel_xe_lpg_ids,
                                  sizeof(intel_xe_lpg_ids) /
                                  sizeof(intel_xe_lpg_ids[0])))
                return NEXUS_GPU_FAMILY_INTEL_XE_LPG;
            return NEXUS_GPU_FAMILY_INTEL_GENERIC;
        case 0x1002U: return NEXUS_GPU_FAMILY_AMD_RADEON;
        case 0x10DEU: return NEXUS_GPU_FAMILY_NVIDIA;
        case 0x1AF4U: return NEXUS_GPU_FAMILY_VIRTIO;
        case 0x15ADU: return NEXUS_GPU_FAMILY_VMWARE;
        case 0x1234U: return NEXUS_GPU_FAMILY_QEMU;
        default: return NEXUS_GPU_FAMILY_UNKNOWN;
    }
}

static const char *family_name(enum nexus_gpu_family family) {
    switch (family) {
        case NEXUS_GPU_FAMILY_INTEL_GENERIC: return "Intel graphics (unclassified)";
        case NEXUS_GPU_FAMILY_INTEL_XE_LPG: return "Intel Xe-LPG";
        case NEXUS_GPU_FAMILY_INTEL_ARC_ALCHEMIST: return "Intel Arc Alchemist (Xe-HPG)";
        case NEXUS_GPU_FAMILY_INTEL_ARC_BATTLEMAGE: return "Intel Arc Battlemage (Xe2)";
        case NEXUS_GPU_FAMILY_INTEL_XE2: return "Intel Xe2 integrated graphics";
        case NEXUS_GPU_FAMILY_INTEL_XE3: return "Intel Xe3";
        case NEXUS_GPU_FAMILY_AMD_RADEON: return "AMD Radeon (architecture unclassified)";
        case NEXUS_GPU_FAMILY_NVIDIA: return "NVIDIA graphics (architecture unclassified)";
        case NEXUS_GPU_FAMILY_VIRTIO: return "VirtIO GPU";
        case NEXUS_GPU_FAMILY_VMWARE: return "VMware SVGA";
        case NEXUS_GPU_FAMILY_QEMU: return "QEMU display adapter";
        default: return "Unknown display family";
    }
}

static void inventory_device_bars(const struct nexus_pci_device *device,
                                  struct nexus_gpu_info *info) {
    if (device == NULL || info == NULL) return;
    for (uint8_t index = 0U; index < NEXUS_PCI_BAR_COUNT; ++index) {
        struct nexus_pci_bar decoded;
        if (!pci_decode_bar(device, index, &decoded) || !decoded.present)
            continue;
        struct nexus_gpu_bar_info *bar = &info->bars[index];
        bar->present = decoded.present;
        bar->assigned = decoded.assigned;
        bar->io_space = decoded.io_space;
        bar->is_64bit = decoded.is_64bit;
        bar->prefetchable = decoded.prefetchable;
        bar->base = decoded.base;
        ++info->bar_count;
        if (!decoded.io_space) {
            ++info->memory_bar_count;
            if (decoded.is_64bit) info->has_64bit_memory_bar = true;
            if (decoded.prefetchable)
                info->has_prefetchable_memory_bar = true;
        }
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
        info->family = identify_family(device->vendor_id, device->device_id);
        info->family_name = family_name(info->family);
        inventory_device_bars(device, info);
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
        serial_write(" [");
        serial_write(primary->family_name);
        serial_write("] vendor/device=");
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
    return primary != NULL &&
           (primary->hardware_2d_acceleration ||
            primary->hardware_3d_acceleration);
}

void gpu_set_firmware_framebuffer_active(bool active) {
    firmware_framebuffer_active = active;
    if (adapter_count != 0U)
        adapters[0].firmware_framebuffer_active = active;
}

void gpu_set_virtio_display_active(bool active) {
    virtio_display_active = active;
}
