#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

#include "acpi.h"
#include "ac97.h"
#include "cpu.h"
#include "fpu.h"
#include "blockdev.h"
#include "boot_payload.h"
#include "boot_pack.h"
#include "app_store.h"
#include "boot_mode.h"
#include "desktop.h"
#include "elf64.h"
#include "e1000.h"
#include "rtl8139.h"
#include "netdev.h"
#include "net.h"
#include "gdt.h"
#include "interrupts.h"
#include "input.h"
#include "ioapic.h"
#include "memory.h"
#include "pci.h"
#include "gpu.h"
#include "wifi_pci.h"
#include "firmware.h"
#include "virtio_net.h"
#include "paging.h"
#include "serial.h"
#include "storage.h"
#include "scheduler.h"
#include "timer.h"
#include "usermode.h"
#include "process.h"
#include "xhci.h"
#include "vfs.h"
#include "service_manager.h"
#include "smp.h"

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    /* Revision 2 adds upright framebuffer rotation metadata. */
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 2
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memory_map_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_firmware_type_request firmware_type_request = {
    .id = LIMINE_FIRMWARE_TYPE_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_smbios_request smbios_request = {
    .id = LIMINE_SMBIOS_REQUEST_ID, .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST_ID,
    .revision = 0,
    .flags = LIMINE_MP_REQUEST_X86_64_X2APIC
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_file_request executable_file_request = {
    .id = LIMINE_EXECUTABLE_FILE_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_cmdline_request executable_cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

static volatile uint32_t scheduler_count_a;
static volatile uint32_t scheduler_count_b;
static volatile uint32_t scheduler_recycle_count;
static volatile uint8_t scheduler_trace[6];
static volatile uint32_t scheduler_trace_count;

static void scheduler_worker_a(void *argument) {
    (void)argument;
    for (uint32_t i = 0; i < 3; ++i) {
        const uint64_t start = timer_ticks();
        while (timer_ticks() == start)
            __asm__ volatile ("pause");
        scheduler_count_a++;
        if (scheduler_trace_count < 6)
            scheduler_trace[scheduler_trace_count++] = 1;
        scheduler_yield();
    }
}

static void scheduler_worker_b(void *argument) {
    (void)argument;
    for (uint32_t i = 0; i < 3; ++i) {
        const uint64_t start = timer_ticks();
        while (timer_ticks() == start)
            __asm__ volatile ("pause");
        scheduler_count_b++;
        if (scheduler_trace_count < 6)
            scheduler_trace[scheduler_trace_count++] = 2;
        scheduler_yield();
    }
}

static volatile uint32_t smp_test_results[64];

static void smp_test_worker(uint32_t cpu_index, uint32_t item_index,
                            void *context) {
    volatile uint32_t *results = (volatile uint32_t *)context;
    if (item_index < 64U) results[item_index] = cpu_index + 1U;
}

#define NEXUS_SMP_ALLOCATOR_TEST_ITEMS 64U

struct smp_allocator_test_context {
    volatile uint64_t *pages;
    volatile uint32_t *results;
};

static volatile uint64_t smp_allocator_test_pages[
    NEXUS_SMP_ALLOCATOR_TEST_ITEMS];
static volatile uint32_t smp_allocator_test_results[
    NEXUS_SMP_ALLOCATOR_TEST_ITEMS];

static void smp_page_allocate_worker(uint32_t cpu_index,
                                     uint32_t item_index,
                                     void *argument) {
    (void)cpu_index;
    struct smp_allocator_test_context *context =
        (struct smp_allocator_test_context *)argument;
    uint64_t page = 0U;
    if (context == NULL || item_index >= NEXUS_SMP_ALLOCATOR_TEST_ITEMS)
        return;
    if (memory_alloc_page(&page)) {
        context->pages[item_index] = page;
        context->results[item_index] = 1U;
    } else {
        context->results[item_index] = 2U;
    }
}

static void smp_page_free_worker(uint32_t cpu_index,
                                 uint32_t item_index,
                                 void *argument) {
    (void)cpu_index;
    struct smp_allocator_test_context *context =
        (struct smp_allocator_test_context *)argument;
    if (context == NULL || item_index >= NEXUS_SMP_ALLOCATOR_TEST_ITEMS)
        return;
    context->results[item_index] =
        memory_free_page(context->pages[item_index]) ? 1U : 2U;
}

static volatile uint32_t scheduler_preempt_a_started;
static volatile uint32_t scheduler_preempt_b_started;
static volatile uint32_t scheduler_preempt_a_observed_b;
static volatile uint32_t scheduler_preempt_b_observed_a;
static volatile uint32_t scheduler_sleep_worker_started;
static volatile uint32_t scheduler_sleep_observer_ran;
static volatile uint32_t scheduler_sleep_worker_result;
static volatile uint64_t scheduler_sleep_elapsed;
static volatile uint32_t scheduler_user_sleep_worker_ran;

static void scheduler_user_sleep_worker(void *argument) {
    (void)argument;
    scheduler_user_sleep_worker_ran = 1U;
}

static void scheduler_sleep_worker(void *argument) {
    (void)argument;
    const uint64_t start = timer_ticks();
    scheduler_sleep_worker_started = 1U;
    scheduler_sleep_worker_result =
        scheduler_sleep_ticks(3U) ? 1U : 2U;
    scheduler_sleep_elapsed = timer_ticks() - start;
}

static void scheduler_sleep_observer(void *argument) {
    (void)argument;
    scheduler_sleep_observer_ran =
        scheduler_sleep_worker_started != 0U ? 1U : 2U;
}

static void scheduler_preempt_worker_a(void *argument) {
    (void)argument;
    scheduler_preempt_a_started = 1U;
    const uint64_t deadline = timer_ticks() + 6U;
    while (scheduler_preempt_b_started == 0U &&
           timer_ticks() < deadline) {
        __asm__ volatile ("pause");
    }
    scheduler_preempt_a_observed_b =
        scheduler_preempt_b_started != 0U ? 1U : 0U;
}

static void scheduler_preempt_worker_b(void *argument) {
    (void)argument;
    scheduler_preempt_b_observed_a =
        scheduler_preempt_a_started != 0U ? 1U : 0U;
    scheduler_preempt_b_started = 1U;
}

static void scheduler_recycle_worker(void *argument) {
    (void)argument;
    ++scheduler_recycle_count;
}

__attribute__((noreturn))
static void halt_forever(void) {
    for (;;) __asm__ volatile ("cli; hlt" ::: "memory");
}

static bool boot_media_provenance_init(void) {
    if (executable_file_request.response == NULL ||
        executable_file_request.response->executable_file == NULL) {
        serial_write("BOOT MEDIA: executable-file response unavailable\r\n");
        return false;
    }
    const struct limine_file *file = executable_file_request.response->executable_file;
    uint8_t part_nonzero=0U,disk_nonzero=0U;
    for(size_t i=0U;i<sizeof(file->gpt_part_uuid);++i)part_nonzero|=((const uint8_t *)&file->gpt_part_uuid)[i];
    for(size_t i=0U;i<sizeof(file->gpt_disk_uuid);++i)disk_nonzero|=((const uint8_t *)&file->gpt_disk_uuid)[i];
    if(part_nonzero&&blockdev_set_boot_media_from_gpt_partition_guid((const uint8_t *)&file->gpt_part_uuid)){
        serial_write("BOOT MEDIA: matched GPT partition GUID\r\n");return true;
    }
    if(disk_nonzero&&blockdev_set_boot_media_from_gpt_disk_guid((const uint8_t *)&file->gpt_disk_uuid)){
        serial_write("BOOT MEDIA: matched GPT disk GUID\r\n");return true;
    }
    if(file->mbr_disk_id&&blockdev_set_boot_media_from_mbr_disk_id(file->mbr_disk_id)){
        serial_write("BOOT MEDIA: matched MBR disk ID\r\n");return true;
    }
    if(file->media_type==LIMINE_MEDIA_TYPE_GENERIC&&
       blockdev_set_boot_media_external_if_unmatched(
           (const uint8_t *)&file->gpt_part_uuid,
           (const uint8_t *)&file->gpt_disk_uuid,
           file->mbr_disk_id)){
        serial_write("BOOT MEDIA: generic source identity does not match any registered disk\r\n");
        return true;
    }
    if(file->media_type==LIMINE_MEDIA_TYPE_OPTICAL&&blockdev_set_boot_media_external(NEXUS_BOOT_SOURCE_OPTICAL)){
        serial_write("BOOT MEDIA: optical source is not a registered disk\r\n");return true;
    }
    if(file->media_type==LIMINE_MEDIA_TYPE_TFTP&&blockdev_set_boot_media_external(NEXUS_BOOT_SOURCE_NETWORK)){
        serial_write("BOOT MEDIA: network source is not a registered disk\r\n");return true;
    }
    serial_write("BOOT MEDIA: provenance ambiguous; installer writes disabled\r\n");
    return false;
}


static const struct limine_framebuffer *service_framebuffer;
static bool service_pci_start(void *c) { (void)c; return pci_init(); }
static bool service_graphics_start(void *c) { (void)c; (void)gpu_init(); return true; }
static bool service_network_devices_start(void *c) {
    (void)c; netdev_init();
    if (e1000_init()) {
        if (e1000_send_arp_probe()) serial_write("NEXUS E1000 TX READY\r\n");
        else serial_write("WARNING: E1000 ARP probe transmit did not complete\r\n");
    }
    if (rtl8139_init()) serial_write("NEXUS RTL8139 TX/RX READY\r\n");
    if (virtio_net_init()) serial_write("NEXUS VIRTIO-NET TX/RX READY\r\n");
    if (!netdev_ready()) return false;
    serial_write("NETWORK LINK: ");
    serial_write(netdev_link_up() ? "UP\r\n" : "DOWN OR UNKNOWN\r\n");
    return true;
}
static bool service_storage_start(void *c) { (void)c; return storage_probe(); }
static bool service_timer_start(void *c) { (void)c; timer_init(100U); return true; }
static bool service_network_start(void *c) { (void)c; return netdev_ready() && net_init(); }
static bool service_input_start(void *c) {
    (void)c; const bool ps2 = input_init(); (void)xhci_init();
    const bool keyboard = xhci_keyboard_ready(), mouse = xhci_mouse_ready();
    if (!ps2 && !keyboard) serial_write("INPUT: no PS/2 or USB HID keyboard detected; keyboard input unavailable\r\n");
    if (keyboard) serial_write("INPUT: USB HID boot keyboard available\r\n");
    if (mouse) serial_write("INPUT: USB HID boot mouse available\r\n");
    return true;
}
static bool service_audio_start(void *c) {
    (void)c; if (ac97_init()) return true;
    serial_write("AUDIO: no supported AC97 device initialized\r\n"); return false;
}
static bool service_desktop_start(void *c) {
    (void)c;
    if (service_framebuffer == NULL) {
        serial_write("DESKTOP: no framebuffer supplied by bootloader\r\n");
        return false;
    }
    return desktop_show(service_framebuffer);
}
static bool service_process_start(void *c) { (void)c; process_init(); return true; }
static bool register_boot_services(void) {
    static const char *const graphics_dep[] = {"platform.pci"};
    static const char *const netdev_dep[] = {"platform.pci"};
    static const char *const storage_dep[] = {"platform.pci"};
    static const char *const net_dep[] = {"network.devices", "platform.timer"};
    static const char *const input_dep[] = {"platform.pci", "platform.timer"};
    static const char *const audio_dep[] = {"platform.pci", "platform.timer"};
    static const char *const desktop_dep[] = {"graphics", "input"};
    static const char *const process_dep[] = {"platform.timer"};
    const struct nexus_service_descriptor defs[] = {
        {"platform.pci", NULL, 0U, service_pci_start, NULL, NULL},
        {"graphics", graphics_dep, 1U, service_graphics_start, NULL, NULL},
        {"network.devices", netdev_dep, 1U, service_network_devices_start, NULL, NULL},
        {"storage", storage_dep, 1U, service_storage_start, NULL, NULL},
        {"platform.timer", NULL, 0U, service_timer_start, NULL, NULL},
        {"network.stack", net_dep, 2U, service_network_start, NULL, NULL},
        {"input", input_dep, 2U, service_input_start, NULL, NULL},
        {"audio", audio_dep, 2U, service_audio_start, NULL, NULL},
        {"desktop", desktop_dep, 2U, service_desktop_start, NULL, NULL},
        {"process.runtime", process_dep, 1U, service_process_start, NULL, NULL}
    };
    nexus_service_manager_init();
    for (size_t i = 0U; i < sizeof(defs) / sizeof(defs[0]); ++i)
        if (!nexus_service_manager_register(&defs[i])) return false;
    return true;
}


void kmain(void) {
    __asm__ volatile ("cli" : : : "memory");
    serial_init();
    blockdev_init();
    serial_write("\r\nNEXUS OS KERNEL STARTED\r\n");
    serial_write("Architecture: x86_64\r\n");
    serial_write("Boot protocol: Limine\r\n");
    if (!register_boot_services()) { serial_write("ERROR: service registry failed\r\n"); halt_forever(); }
    if (nexus_cpu_init()) {
        if (nexus_fpu_init()) {
            serial_write("CPU: isolated x87/SSE2 state enabled for foreground apps\r\n");
            if (nexus_fpu_avx_available()) {
                serial_write("CPU: XSAVE AVX/YMM state enabled for native apps\r\n");
            } else {
                serial_write("CPU: AVX unavailable; using legacy FXSAVE/SSE2 state\r\n");
            }
        } else {
            serial_write("WARNING: x87/SSE2 app runtime unavailable on this CPU\r\n");
        }
        const struct nexus_cpu_info *cpu = nexus_cpu_get_info();
        serial_write("CPU: ");
        serial_write(cpu->brand);
        serial_write(" [");
        serial_write(cpu->vendor);
        serial_write("] family/model/stepping=");
        serial_write_hex(cpu->family);
        serial_write("/");
        serial_write_hex(cpu->model);
        serial_write("/");
        serial_write_hex(cpu->stepping);
        serial_write(" cores/logical=");
        serial_write_hex(cpu->physical_cores_per_package);
        serial_write("/");
        serial_write_hex(cpu->logical_processors_per_package);
        serial_write(" features=");
        serial_write(nexus_cpu_feature_level_label());
        serial_write("\r\nNEXUS CPU CAPABILITY DISCOVERY READY\r\n");
        serial_write("CPU vendor family: ");
        serial_write(nexus_cpu_vendor_name());
        serial_write("\r\n");
        if (cpu->core_type == NEXUS_CPU_CORE_TYPE_PERFORMANCE)
            serial_write("CPU bootstrap core type: Intel performance core\r\n");
        else if (cpu->core_type == NEXUS_CPU_CORE_TYPE_EFFICIENCY)
            serial_write("CPU bootstrap core type: Intel efficiency core\r\n");
        if (cpu->frequency_info_valid) {
            serial_write("CPU nominal base/max/bus MHz: ");
            serial_write_hex(cpu->base_frequency_mhz);
            serial_write("/");
            serial_write_hex(cpu->maximum_frequency_mhz);
            serial_write("/");
            serial_write_hex(cpu->bus_frequency_mhz);
            serial_write("\r\n");
        }
        serial_write("CPU advertised DTS/Turbo/HWP: ");
        serial_write(cpu->digital_thermal_sensor_supported ? "yes" : "no");
        serial_write("/");
        serial_write(cpu->turbo_boost_supported ? "yes" : "no");
        serial_write("/");
        serial_write(cpu->hardware_p_states_supported ? "yes" : "no");
        serial_write("\r\n");
    } else {
        serial_write("WARNING: CPU CPUID capability discovery unavailable\r\n");
    }

    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
        serial_write("ERROR: unsupported Limine base revision\r\n");
        halt_forever();
    }

    if (nexus_boot_payloads_init((void *)module_request.response,
                                 (void *)executable_file_request.response)) {
        serial_write("INSTALLER: verified kernel, UEFI loader, BIOS stage-2, and system-pack payloads available\r\n");
        const struct nexus_boot_payloads *boot_payloads =
            nexus_boot_payloads_get();
        if (boot_payloads != NULL &&
            nexus_boot_pack_init(boot_payloads->system_pack.data,
                                 boot_payloads->system_pack.size)) {
            serial_write("NEXUS READ-ONLY SYSTEM PACK READY; files=");
            serial_write_hex(nexus_boot_pack_count());
            serial_write("\r\n");
        } else {
            serial_write("WARNING: system pack checksum/format validation failed; bundled apps unavailable and installation blocked\r\n");
        }
    } else {
        serial_write("INSTALLER: boot payload set unavailable or invalid; deployment disabled\r\n");
        struct limine_module_response *mods =
            (struct limine_module_response *)module_request.response;
        if (mods == NULL) {
            serial_write("INSTALLER DEBUG: module response is null\r\n");
        } else {
            serial_write("INSTALLER DEBUG: module count ");
            serial_write_hex(mods->module_count);
            serial_write("\r\n");
            if (mods->modules == NULL) {
                serial_write("INSTALLER DEBUG: module array is null\r\n");
            } else {
                uint64_t limit = mods->module_count < 8U ? mods->module_count : 8U;
                for (uint64_t i = 0U; i < limit; ++i) {
                    struct limine_file *file = mods->modules[i];
                    serial_write("INSTALLER DEBUG: module path ");
                    if (file != NULL && file->path != NULL)
                        serial_write(file->path);
                    else
                        serial_write("(null)");
                    serial_write("\r\n");
                }
            }
        }
    }

    if (!gdt_init()) {
        serial_write("ERROR: Nexus GDT/TSS initialization failed\r\n");
        halt_forever();
    }
    serial_write("NEXUS GDT TSS READY\r\n");

    idt_init();
    serial_write("K1: CPU exception IDT installed (vectors 0-31)\r\n");

    if (!memory_init(memory_map_request.response, hhdm_request.response)) {
        serial_write("ERROR: physical memory map initialization failed\r\n");
        halt_forever();
    }

    if (paging_enable_nx()) {
        serial_write("NEXUS NX PAGE PROTECTION READY\r\n");
    } else {
        serial_write("WARNING: CPU does not support NX page protection\r\n");
    }

    if (!memory_allocator_selftest()) {
        serial_write("ERROR: physical page reclamation self-test failed\r\n");
        halt_forever();
    }
    serial_write("NEXUS PAGE RECLAIM READY\r\n");

    if (!paging_user_address_space_selftest()) {
        serial_write("ERROR: user address-space isolation self-test failed\r\n");
        halt_forever();
    }
    serial_write("NEXUS USER ADDRESS-SPACE ISOLATION READY\r\n");

    /* ELF loading tests allocate and map real pages; run only after memory and
       paging initialization, never before memory_init(). */
    if (!nexus_elf64_selftest()) {
        serial_write("ERROR: ELF64 ET_EXEC/static-PIE loader self-test failed\r\n");
        halt_forever();
    }
    serial_write("NEXUS ELF64 STATIC-PIE VALIDATION READY\r\n");

    uint64_t first_page = 0;
    if (!memory_alloc_page(&first_page) ||
        memory_phys_to_virt(first_page) == NULL) {
        serial_write("ERROR: physical page allocation failed\r\n");
        halt_forever();
    }

    uint64_t dma32_page = 0;
    if (memory_alloc_dma_page(0x100000000ULL, &dma32_page) &&
        dma32_page < 0x100000000ULL) {
        serial_write("K1: DMA32 page allocated at: ");
        serial_write_hex(dma32_page);
        serial_write("\r\n");
        serial_write("NEXUS DMA32 PAGE ALLOCATOR READY\r\n");
    } else {
        serial_write("WARNING: no page available below the 4 GiB DMA limit\r\n");
    }

    serial_write("K1: physical memory allocator ready\r\n");
    serial_write("Usable memory bytes: ");
    serial_write_hex(memory_total_bytes());
    serial_write("\r\nRemaining bootstrap pages bytes: ");
    serial_write_hex(memory_free_bytes());
    serial_write("\r\nFirst allocated physical page: ");
    serial_write_hex(first_page);
    serial_write("\r\n");

    struct memory_allocator_stats allocator_stats;
    if (memory_get_allocator_stats(&allocator_stats)) {
        serial_write("NEXUS MEMORY ALLOCATOR DIAGNOSTICS READY\r\n");
        serial_write("Physical memory regions: ");
        serial_write_hex(allocator_stats.region_count);
        serial_write("\r\nLargest unallocated extent bytes: ");
        serial_write_hex(allocator_stats.largest_unallocated_extent_bytes);
        serial_write("\r\nReclaimed free-list pages: ");
        serial_write_hex(allocator_stats.free_list_pages);
        serial_write("\r\nAllocation failures: ");
        serial_write_hex(allocator_stats.allocation_failures);
        serial_write("\r\nDMA zone total/free bytes: ");
        serial_write_hex(allocator_stats.dma_total_bytes);
        serial_write("/");
        serial_write_hex(allocator_stats.dma_free_bytes);
        serial_write("\r\nDMA32 zone total/free bytes: ");
        serial_write_hex(allocator_stats.dma32_total_bytes);
        serial_write("/");
        serial_write_hex(allocator_stats.dma32_free_bytes);
        serial_write("\r\nNormal zone total/free bytes: ");
        serial_write_hex(allocator_stats.normal_total_bytes);
        serial_write("/");
        serial_write_hex(allocator_stats.normal_free_bytes);
        serial_write("\r\nNEXUS DMA MEMORY ZONE ACCOUNTING READY\r\n");
    }

    if (rsdp_request.response == NULL ||
        !acpi_init(rsdp_request.response->address)) {
        serial_write("ERROR: ACPI RSDP/MADT discovery failed\r\n");
        halt_forever();
    }
    serial_write("NEXUS ACPI MADT READY\r\n");

    const uint64_t firmware_type =
        firmware_type_request.response != NULL
            ? firmware_type_request.response->firmware_type : UINT64_MAX;
    const void *smbios32 = smbios_request.response != NULL
        ? smbios_request.response->entry_32 : NULL;
    const void *smbios64 = smbios_request.response != NULL
        ? smbios_request.response->entry_64 : NULL;
    (void)nexus_firmware_init(firmware_type, smbios32, smbios64,
                              rsdp_request.response->address);
    const struct nexus_firmware_info *firmware = nexus_firmware_get_info();
    serial_write("FIRMWARE: ");
    serial_write(nexus_firmware_type_label());
    if (firmware->smbios_present) {
        serial_write(" SMBIOS ");
        serial_write_hex(firmware->smbios_major);
        serial_write(".");
        serial_write_hex(firmware->smbios_minor);
    } else {
        serial_write(" SMBIOS UNAVAILABLE");
    }
    serial_write("\r\nNEXUS FIRMWARE INVENTORY READY\r\n");

    if (!nexus_service_manager_start("platform.pci")) {
        serial_write("ERROR: PCI configuration-space scan found no devices\r\n"); halt_forever();
    }
    serial_write("NEXUS PCI DISCOVERY READY\r\n");
    if (!nexus_service_manager_start("graphics"))
        serial_write("WARNING: graphics discovery service failed\r\n");
    if (wifi_pci_discover()) {
        serial_write("WIFI: PCI wireless-class candidates discovered: ");
        serial_write_hex(wifi_pci_candidate_count());
        serial_write("\r\n");
        for (uint32_t wifi_index = 0U;
             wifi_index < wifi_pci_candidate_count(); ++wifi_index) {
            const struct nexus_wifi_pci_candidate *candidate =
                wifi_pci_candidate_at(wifi_index);
            if (candidate == NULL) continue;
            serial_write("WIFI CANDIDATE: ");
            serial_write(candidate->vendor_name);
            serial_write(" id=");
            serial_write_hex(candidate->vendor_id);
            serial_write("/");
            serial_write_hex(candidate->device_id);
            serial_write(" bdf=");
            serial_write_hex(candidate->bus);
            serial_write(":");
            serial_write_hex(candidate->slot);
            serial_write(".");
            serial_write_hex(candidate->function);
            serial_write(" (inventory only; chipset driver required)\r\n");
        }
    } else {
        serial_write("WIFI: no PCI wireless-class device found; USB/radio firmware support may vary\r\n");
    }
    serial_write("PCI: retained network controllers (class 02):\r\n");
    for (uint32_t pci_index = 0U;
         pci_index < pci_stored_device_count(); ++pci_index) {
        const struct nexus_pci_device *network_controller =
            pci_device_at(pci_index);
        if (network_controller == NULL ||
            network_controller->class_code != 0x02U) continue;
        serial_write("PCI NET: ");
        serial_write_hex(network_controller->bus);
        serial_write(":");
        serial_write_hex(network_controller->slot);
        serial_write(".");
        serial_write_hex(network_controller->function);
        serial_write(" id=");
        serial_write_hex(network_controller->vendor_id);
        serial_write("/");
        serial_write_hex(network_controller->device_id);
        serial_write(" subclass/prog-if=");
        serial_write_hex(network_controller->subclass);
        serial_write("/");
        serial_write_hex(network_controller->programming_interface);
        serial_write("\r\n");
    }
    serial_write("PCI: retained functions=");
    serial_write_hex(pci_stored_device_count());
    serial_write(" total discovered=");
    serial_write_hex(pci_device_count());
    serial_write("\r\n");

    if (!nexus_service_manager_start("network.devices"))
        serial_write("WARNING: no supported Ethernet driver initialized\r\n");

    if (!ioapic_probe()) {
        serial_write("ERROR: I/O APIC read-only probe failed\r\n");
        halt_forever();
    }
    serial_write("NEXUS IOAPIC PROBE READY\r\n");

    if (!nexus_service_manager_start("storage"))
        serial_write("WARNING: no supported mass-storage driver initialized\r\n");
    serial_write("NEXUS STORAGE DISCOVERY READY\r\n");
    const bool boot_media_identity_ready = boot_media_provenance_init();
    if (!boot_media_identity_ready) {
        serial_write("INSTALLER: destructive writes locked until boot source is identified\r\n");
    }
#ifdef NEXUS_STORAGE_SELFTEST
    if (!storage_run_selftests()) {
        serial_write("ERROR: post-provenance storage write self-test failed\r\n");
        halt_forever();
    }
#endif

    if (!nexus_service_manager_start("platform.timer")) {
        serial_write("ERROR: platform timer service failed to start\r\n"); halt_forever();
    }
    if (!nexus_service_manager_start("network.stack"))
        serial_write("WARNING: network stack unavailable; continuing offline\r\n");
    serial_write("IOAPIC: direct timer routing held pending interrupt-frame validation; using PIC/PIT virtual-wire mode\r\n");

    serial_write("K2: PIT configured for 100 Hz\r\n");

    const uint64_t target_tick = timer_ticks() + 5;
    __asm__ volatile ("sti" : : : "memory");
    while (timer_ticks() < target_tick) {
        __asm__ volatile ("hlt" : : : "memory");
    }
    __asm__ volatile ("cli" : : : "memory");

    serial_write("K2: hardware timer ticks received: ");
    serial_write_hex(timer_ticks());
    serial_write("\r\n");

    serial_write("K2: scheduler self-test initialization\r\n");
    if (!nexus_smp_init(mp_request.response)) {
        serial_write("SMP: initialization failed; boot continuing\r\n");
    } else {
        for (size_t i = 0U; i < 64U; ++i) smp_test_results[i] = 0U;
        if (!nexus_smp_parallel_for(64U, smp_test_worker,
                                    (void *)smp_test_results)) {
            serial_write("SMP: parallel worker test failed\r\n");
            halt_forever();
        }
        for (size_t i = 0U; i < 64U; ++i) {
            if (smp_test_results[i] == 0U) {
                serial_write("SMP: parallel worker left an item unprocessed\r\n");
                halt_forever();
            }
        }
        serial_write("NEXUS SMP PARALLEL WORK TEST READY\r\n");

        uint32_t performance_cores = 0U;
        uint32_t efficiency_cores = 0U;
        uint32_t unclassified_cores = 0U;
        uint32_t intel_cpus = 0U;
        uint32_t amd_cpus = 0U;
        uint32_t other_vendor_cpus = 0U;
        uint32_t amd_topology_records = 0U;
        uint32_t inventory_online_count = 0U;
        for (uint32_t cpu_slot = 0U;
             cpu_slot < NEXUS_SMP_MAX_CPUS; ++cpu_slot) {
            struct nexus_smp_cpu_info cpu_topology;
            if (!nexus_smp_cpu_info_at(cpu_slot, &cpu_topology)) break;
            if (!cpu_topology.online) continue;
            ++inventory_online_count;
            if (cpu_topology.vendor == NEXUS_CPU_VENDOR_INTEL)
                ++intel_cpus;
            else if (cpu_topology.vendor == NEXUS_CPU_VENDOR_AMD ||
                     cpu_topology.vendor == NEXUS_CPU_VENDOR_HYGON)
                ++amd_cpus;
            else
                ++other_vendor_cpus;
            if ((cpu_topology.vendor == NEXUS_CPU_VENDOR_AMD ||
                 cpu_topology.vendor == NEXUS_CPU_VENDOR_HYGON) &&
                cpu_topology.topology_extensions_supported &&
                cpu_topology.apic_id_valid &&
                cpu_topology.core_id_valid && cpu_topology.node_id_valid)
                ++amd_topology_records;
            if (cpu_topology.core_type == NEXUS_CPU_CORE_TYPE_PERFORMANCE)
                ++performance_cores;
            else if (cpu_topology.core_type == NEXUS_CPU_CORE_TYPE_EFFICIENCY)
                ++efficiency_cores;
            else
                ++unclassified_cores;
        }
        if (inventory_online_count != nexus_smp_online_cpu_count()) {
            serial_write("ERROR: SMP online count disagrees with CPU topology inventory\r\n");
            halt_forever();
        }
        serial_write("SMP CPU vendor inventory Intel/AMD/other=");
        serial_write_hex(intel_cpus);
        serial_write("/");
        serial_write_hex(amd_cpus);
        serial_write("/");
        serial_write_hex(other_vendor_cpus);
        serial_write(" AMD extended topology records=");
        serial_write_hex(amd_topology_records);
        serial_write("\r\n");
        serial_write("SMP core inventory P/E/other=");
        serial_write_hex(performance_cores);
        serial_write("/");
        serial_write_hex(efficiency_cores);
        serial_write("/");
        serial_write_hex(unclassified_cores);
        serial_write("\r\n");


        struct smp_allocator_test_context allocator_context = {
            .pages = smp_allocator_test_pages,
            .results = smp_allocator_test_results
        };
        for (size_t i = 0U; i < NEXUS_SMP_ALLOCATOR_TEST_ITEMS; ++i) {
            smp_allocator_test_pages[i] = 0U;
            smp_allocator_test_results[i] = 0U;
        }
        if (!nexus_smp_parallel_for(NEXUS_SMP_ALLOCATOR_TEST_ITEMS,
                                   smp_page_allocate_worker,
                                   &allocator_context)) {
            serial_write("ERROR: SMP page-allocation stress dispatch failed\r\n");
            halt_forever();
        }
        bool allocator_unique = true;
        for (size_t i = 0U; i < NEXUS_SMP_ALLOCATOR_TEST_ITEMS; ++i) {
            if (smp_allocator_test_results[i] != 1U ||
                smp_allocator_test_pages[i] == 0U ||
                !memory_page_is_allocated(smp_allocator_test_pages[i])) {
                allocator_unique = false;
                break;
            }
            for (size_t j = i + 1U;
                 j < NEXUS_SMP_ALLOCATOR_TEST_ITEMS; ++j) {
                if (smp_allocator_test_pages[i] ==
                    smp_allocator_test_pages[j]) {
                    allocator_unique = false;
                    break;
                }
            }
            if (!allocator_unique) break;
        }
        if (!allocator_unique) {
            serial_write("ERROR: concurrent CPUs received duplicate/invalid pages\r\n");
            halt_forever();
        }

        for (size_t i = 0U; i < NEXUS_SMP_ALLOCATOR_TEST_ITEMS; ++i)
            smp_allocator_test_results[i] = 0U;
        if (!nexus_smp_parallel_for(NEXUS_SMP_ALLOCATOR_TEST_ITEMS,
                                   smp_page_free_worker,
                                   &allocator_context)) {
            serial_write("ERROR: SMP page-free stress dispatch failed\r\n");
            halt_forever();
        }
        for (size_t i = 0U; i < NEXUS_SMP_ALLOCATOR_TEST_ITEMS; ++i) {
            if (smp_allocator_test_results[i] != 1U ||
                memory_page_is_allocated(smp_allocator_test_pages[i])) {
                serial_write("ERROR: concurrent page reclamation failed\r\n");
                halt_forever();
            }
        }
        if (!memory_allocator_validate()) {
            serial_write("ERROR: allocator invariants failed after SMP stress\r\n");
            halt_forever();
        }
        serial_write("NEXUS PAGE ALLOCATOR PARALLEL TEST PASSED; CPUs=");
        serial_write_hex(nexus_smp_online_cpu_count());
        serial_write(" pages=");
        serial_write_hex(NEXUS_SMP_ALLOCATOR_TEST_ITEMS);
        serial_write("\r\n");
    }

    scheduler_init();
    scheduler_count_a = 0;
    scheduler_count_b = 0;
    scheduler_recycle_count = 0;
    scheduler_trace_count = 0;

    if (!scheduler_create_thread(scheduler_worker_a, NULL) ||
        !scheduler_create_thread(scheduler_worker_b, NULL)) {
        serial_write("ERROR: unable to create cooperative scheduler test threads\r\n");
        halt_forever();
    }

    serial_write("K2: scheduler threads created\r\n");
    scheduler_start();
    serial_write("K2: scheduler start returned\r\n");
    scheduler_wait();
    serial_write("K2: initial scheduler workers finished\r\n");

    bool scheduler_ok = scheduler_count_a == 3 &&
                        scheduler_count_b == 3 &&
                        scheduler_trace_count == 6;
    static const uint8_t expected_trace[6] = {1, 2, 1, 2, 1, 2};
    for (uint32_t i = 0; i < 6 && scheduler_ok; ++i) {
        if (scheduler_trace[i] != expected_trace[i]) scheduler_ok = false;
    }

    if (!scheduler_ok) {
        serial_write("ERROR: cooperative scheduler round-robin test failed\r\n");
        halt_forever();
    }

    /*
     * Exercise the full sixteen-thread capacity, reject overflow, then prove
     * completed slots can be reused without growing the scheduler table.
     */
    for (uint32_t i = 0U; i < NEXUS_SCHEDULER_MAX_THREADS; ++i) {
        if (!scheduler_create_thread(scheduler_recycle_worker, NULL)) {
            serial_write("ERROR: scheduler could not fill all thread slots\r\n");
            halt_forever();
        }
    }
    if (scheduler_create_thread(scheduler_recycle_worker, NULL)) {
        serial_write("ERROR: scheduler accepted a thread beyond its capacity\r\n");
        halt_forever();
    }
    scheduler_wait();
    serial_write("K2: all sixteen worker slots completed\r\n");
    if (scheduler_recycle_count != NEXUS_SCHEDULER_MAX_THREADS ||
        !scheduler_create_thread(scheduler_recycle_worker, NULL)) {
        serial_write("ERROR: scheduler failed to recycle finished thread slots\r\n");
        halt_forever();
    }
    scheduler_wait();
    serial_write("K2: recycled worker finished\r\n");
    if (scheduler_recycle_count != NEXUS_SCHEDULER_MAX_THREADS + 1U) {
        serial_write("ERROR: scheduler recycled-thread self-test failed\r\n");
        halt_forever();
    }
    serial_write("K2: cooperative scheduling and thread-slot recycling passed\r\n");

    /*
     * Timed sleeps should yield the CPU, allow another worker to run, and
     * resume only after the PIT deadline has elapsed. The BSP main thread is
     * retained as the non-blockable idle context at this stage.
     */
    scheduler_sleep_worker_started = 0U;
    scheduler_sleep_observer_ran = 0U;
    scheduler_sleep_worker_result = 0U;
    scheduler_sleep_elapsed = 0U;
    if (!scheduler_sleep_ticks(1U) ||
        !scheduler_create_thread(scheduler_sleep_worker, NULL) ||
        !scheduler_create_thread(scheduler_sleep_observer, NULL)) {
        serial_write("ERROR: scheduler timed-sleep setup failed\r\n");
        halt_forever();
    }
    scheduler_wait();
    if (scheduler_sleep_worker_started == 0U ||
        scheduler_sleep_observer_ran != 1U ||
        scheduler_sleep_worker_result != 1U ||
        scheduler_sleep_elapsed < 3U) {
        serial_write("ERROR: timer-driven scheduler sleep/wakeup test failed\r\n");
        halt_forever();
    }
    serial_write("K2: timed worker sleep, wakeup and CPU yielding passed\r\n");

    /*
     * Verify timer-driven switching between CPU-bound kernel threads without
     * explicit yields. Preemption remains off outside this isolated test until
     * allocator, VFS, driver and user-process state are all safe to preempt.
     */
    scheduler_preempt_a_started = 0U;
    scheduler_preempt_b_started = 0U;
    scheduler_preempt_a_observed_b = 0U;
    scheduler_preempt_b_observed_a = 0U;
    if (!scheduler_create_thread(scheduler_preempt_worker_a, NULL) ||
        !scheduler_create_thread(scheduler_preempt_worker_b, NULL)) {
        serial_write("ERROR: unable to create timer-preemption test threads\r\n");
        halt_forever();
    }
    scheduler_set_preemption_enabled(true);
    scheduler_start();
    scheduler_wait();
    scheduler_set_preemption_enabled(false);
    if (scheduler_preempt_a_observed_b == 0U ||
        scheduler_preempt_b_observed_a == 0U) {
        serial_write("ERROR: timer IRQ did not preempt CPU-bound kernel threads\r\n");
        halt_forever();
    }
    serial_write("K2: timer IRQ preemption between kernel threads passed\r\n");

    const char *boot_command_line = NULL;
    if (executable_cmdline_request.response != NULL)
        boot_command_line = executable_cmdline_request.response->cmdline;
    const bool installer_boot = nexus_boot_mode_is_installer(boot_command_line);
    desktop_set_installer_mode(installer_boot);
    serial_write(installer_boot
        ? "NEXUS BOOT MODE: DEDICATED INSTALLER\r\n"
        : "NEXUS BOOT MODE: DESKTOP\r\n");

    service_framebuffer = NULL;
    if (framebuffer_request.response != NULL &&
        framebuffer_request.response->framebuffer_count > 0)
        service_framebuffer = framebuffer_request.response->framebuffers[0];
    if (!nexus_service_manager_start("input"))
        serial_write("WARNING: input service did not initialize\r\n");
    if (!nexus_service_manager_start("audio"))
        serial_write("AUDIO: service unavailable; continuing without audio\r\n");
    if (!nexus_service_manager_start("desktop"))
        serial_write("WARNING: desktop service did not initialize\r\n");
    serial_write("NEXUS K2 MULTITASKING SCHEDULER READY\r\n");
    serial_write("NEXUS K2 TIMER INTERRUPT READY\r\n");
    serial_write("NEXUS K1 MEMORY+EXCEPTIONS READY\r\n");
    if (!nexus_service_manager_start("process.runtime")) {
        serial_write("ERROR: native process runtime service failed\r\n"); halt_forever();
    }
    scheduler_user_sleep_worker_ran = 0U;
    if (!scheduler_create_thread(scheduler_user_sleep_worker, NULL)) {
        serial_write("ERROR: unable to create user-sleep scheduling worker\r\n");
        halt_forever();
    }
    if (!usermode_run_selftest() ||
        scheduler_user_sleep_worker_ran != 1U) {
        serial_write("ERROR: CPL3 sleep syscall did not yield to a kernel worker\r\n");
        halt_forever();
    }
    serial_write("NEXUS USER SLEEP SYSCALL SCHEDULER INTEGRATION READY\r\n");
    scheduler_wait();
#ifdef NEXUS_STORAGE_SELFTEST
    const enum nexus_desktop_process_test_result file_test =
        desktop_run_process_selftest();
    if (file_test == NEXUS_DESKTOP_PROCESS_TEST_FAILED) {
        serial_write("ERROR: FAT32 ELF file execution self-test failed\r\n");
        halt_forever();
    } else if (file_test == NEXUS_DESKTOP_PROCESS_TEST_PASSED) {
        serial_write("NEXUS FILE PROCESS SELFTEST READY\r\n");
    } else {
        serial_write("NEXUS FILE PROCESS SELFTEST SKIPPED\r\n");
    }
#endif
    if (!vfs_ramfs_selftest()) {
        serial_write("ERROR: RAM filesystem self-test failed\r\n");
        halt_forever();
    }
#ifdef NEXUS_STORAGE_SELFTEST
    uint64_t store_pie_exit_status = UINT64_MAX;
    if (!nexus_store_launch_native(NEXUS_STORE_NATIVE_PIE_INDEX,
                                   &store_pie_exit_status) ||
        store_pie_exit_status != 0U) {
        serial_write("ERROR: bundled native Store application self-test failed\r\n");
        halt_forever();
    }
    serial_write("NEXUS STORE BUNDLED APP SELFTEST READY\r\n");
#endif
    serial_write("NEXUS RAMFS READY\r\n");
    serial_write("NEXUS PROCESS MANAGER READY\r\n");
    serial_write("NEXUS ELF64 USER LOADER READY\r\n");
    serial_write("NEXUS USER MODE SELFTEST READY\r\n");
    serial_write("NEXUS SYSCALL ABI READY\r\n");
    serial_write("NEXUS K0 BOOT COMPLETE\r\n");

    /* Timer and input IRQs wake the kernel while the event queue is drained. */
    __asm__ volatile ("sti" : : : "memory");
    for (;;) {
        net_poll();
        xhci_poll();
        ac97_poll();
        struct nexus_input_event event;
        while (input_pop_event(&event)) {
            desktop_handle_input(&event);
            if (event.type == NEXUS_INPUT_KEYBOARD) {
                serial_write("INPUT: keyboard scancode=");
                serial_write_hex(event.scancode);
                serial_write("\r\n");
            } else if (event.type == NEXUS_INPUT_MOUSE) {
                serial_write("INPUT: mouse dx=");
                serial_write_hex((uint16_t)event.mouse_dx);
                serial_write(" dy=");
                serial_write_hex((uint16_t)event.mouse_dy);
                serial_write(" buttons=");
                serial_write_hex(event.mouse_buttons);
                serial_write("\r\n");
            }
        }
        __asm__ volatile ("sti; hlt" : : : "memory");
    }
}
