#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

#include "acpi.h"
#include "blockdev.h"
#include "boot_payload.h"
#include "desktop.h"
#include "elf64.h"
#include "e1000.h"
#include "net.h"
#include "gdt.h"
#include "interrupts.h"
#include "input.h"
#include "ioapic.h"
#include "memory.h"
#include "pci.h"
#include "paging.h"
#include "serial.h"
#include "storage.h"
#include "scheduler.h"
#include "timer.h"
#include "usermode.h"
#include "process.h"
#include "xhci.h"
#include "wifi.h"
#include "vfs.h"

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0
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
static volatile struct limine_executable_file_request executable_file_request = {
    .id = LIMINE_EXECUTABLE_FILE_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

static volatile uint32_t scheduler_count_a;
static volatile uint32_t scheduler_count_b;
static volatile uint8_t scheduler_trace[6];
static volatile uint32_t scheduler_trace_count;
static volatile uint32_t scheduler_reuse_count;
static volatile uint32_t scheduler_capacity_count;

static void scheduler_reuse_worker(void *argument) {
    (void)argument;
    ++scheduler_reuse_count;
}

static void scheduler_capacity_worker(void *argument) {
    (void)argument;
    ++scheduler_capacity_count;
}

static void scheduler_worker_a(void *argument) {
    (void)argument;
    for (uint32_t i = 0; i < 3; ++i) {
        const uint64_t start = timer_ticks();
        while (timer_ticks() == start)
            __asm__ volatile ("pause");
        scheduler_count_a++;
        if (scheduler_trace_count < 6)
            scheduler_trace[scheduler_trace_count++] = 1;
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
    }
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

void kmain(void) {
    __asm__ volatile ("cli" : : : "memory");
    serial_init();
    blockdev_init();
    serial_write("\r\nNEXUS OS KERNEL STARTED\r\n");
    serial_write("Architecture: x86_64\r\n");
    serial_write("Boot protocol: Limine\r\n");
    if (!nexus_elf64_selftest()) {
        serial_write("ERROR: ELF64 parser self-test failed\r\n");
        halt_forever();
    }
    serial_write("NEXUS ELF64 VALIDATION READY\r\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
        serial_write("ERROR: unsupported Limine base revision\r\n");
        halt_forever();
    }

    const bool boot_payloads_ready = nexus_boot_payloads_init(
        (void *)module_request.response,
        (void *)executable_file_request.response);

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

    if (rsdp_request.response == NULL ||
        !acpi_init(rsdp_request.response->address)) {
        serial_write("ERROR: ACPI RSDP/MADT discovery failed\r\n");
        halt_forever();
    }
    serial_write("NEXUS ACPI MADT READY\r\n");

    if (!pci_init()) {
        serial_write("ERROR: PCI configuration-space scan found no devices\r\n");
        halt_forever();
    }
    serial_write("NEXUS PCI DISCOVERY READY\r\n");

    if (e1000_init()) {
        if (e1000_send_arp_probe()) {
            serial_write("NEXUS E1000 TX READY\r\n");
        } else {
            serial_write("WARNING: E1000 ARP probe transmit did not complete\r\n");
        }
    }


    if (!ioapic_probe()) {
        serial_write("ERROR: I/O APIC read-only probe failed\r\n");
        halt_forever();
    }
    serial_write("NEXUS IOAPIC PROBE READY\r\n");

    if (!storage_probe()) {
        serial_write("WARNING: no mass-storage PCI controller discovered\r\n");
    }
    serial_write("NEXUS STORAGE DISCOVERY READY\r\n");
    const bool boot_media_identity_ready = boot_media_provenance_init();
    if (!boot_media_identity_ready) {
        serial_write("INSTALLER: destructive writes locked until boot source is identified\r\n");
    }
#if defined(NEXUS_INSTALLER_BUILD)
    if (!boot_media_identity_ready || !blockdev_boot_source_is_external()) {
        serial_write("INSTALLER ERROR: live-media boot source was not positively identified\r\n");
        halt_forever();
    }
    if (!boot_payloads_ready) {
        serial_write("INSTALLER ERROR: separate installed-OS payload is missing or invalid\r\n");
        halt_forever();
    }
    serial_write("INSTALLER: separate installed OS and live-installer images verified\r\n");
#elif defined(NEXUS_INSTALLED_OS_BUILD)
    (void)boot_payloads_ready;
    serial_write("NEXUS OS RUNTIME: separate installed-system kernel; installer mode disabled\r\n");
#else
    if (boot_payloads_ready)
        serial_write("INSTALLER: boot payload set verified\r\n");
    else
        serial_write("INSTALLER: boot payload set unavailable; deployment disabled\r\n");
#endif
#ifdef NEXUS_STORAGE_SELFTEST
    if (!storage_run_selftests()) {
        serial_write("ERROR: post-provenance storage write self-test failed\r\n");
        halt_forever();
    }
#endif

    timer_init(100);
    if (e1000_ready() && !net_init()) {
        serial_write("WARNING: Ethernet network layer did not initialize\r\n");
    }
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

    scheduler_init();
    scheduler_count_a = 0;
    scheduler_count_b = 0;
    scheduler_trace_count = 0;

    if (!scheduler_create_thread(scheduler_worker_a, NULL) ||
        !scheduler_create_thread(scheduler_worker_b, NULL)) {
        serial_write("ERROR: unable to create cooperative scheduler test threads\r\n");
        halt_forever();
    }

    scheduler_start();
    scheduler_wait();

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
    serial_write("K2: preemptive kernel-thread test passed (timer-driven A/B alternation)\r\n");

    /* All four worker slots can be active; a fifth must be rejected. */
    scheduler_capacity_count = 0U;
    bool scheduler_capacity_ok = true;
    for (uint32_t i = 0U; i < 4U; ++i) {
        if (!scheduler_create_thread(scheduler_capacity_worker, NULL)) {
            scheduler_capacity_ok = false;
            break;
        }
    }
    if (scheduler_capacity_ok &&
        scheduler_create_thread(scheduler_capacity_worker, NULL)) {
        scheduler_capacity_ok = false;
    }
    if (scheduler_capacity_ok) scheduler_wait();
    if (!scheduler_capacity_ok || scheduler_capacity_count != 4U) {
        serial_write("ERROR: scheduler four-slot capacity test failed\r\n");
        halt_forever();
    }
    serial_write("K2: four concurrent thread slots and capacity guard passed\r\n");

    /* More sequential workers than slots verifies finished-slot reclamation. */
    scheduler_reuse_count = 0U;
    bool scheduler_reuse_ok = true;
    for (uint32_t i = 0U; i < 12U; ++i) {
        if (!scheduler_create_thread(scheduler_reuse_worker, NULL)) {
            scheduler_reuse_ok = false;
            break;
        }
        scheduler_yield();
        if (scheduler_reuse_count != i + 1U) {
            scheduler_reuse_ok = false;
            break;
        }
    }
    if (!scheduler_reuse_ok) {
        serial_write("ERROR: scheduler thread-slot reuse test failed\\r\\n");
        halt_forever();
    }
    serial_write("K2: scheduler reclaims finished thread slots (12 sequential workers)\\r\\n");

    if (framebuffer_request.response != NULL &&
        framebuffer_request.response->framebuffer_count > 0) {
        (void)desktop_show(framebuffer_request.response->framebuffers[0]);
    } else {
        serial_write("DESKTOP: no framebuffer supplied by bootloader\r\n");
    }
    serial_write("NEXUS K2 PREEMPTIVE THREAD SCHEDULER READY\r\n");
    serial_write("NEXUS K2 TIMER INTERRUPT READY\r\n");
    serial_write("NEXUS K1 MEMORY+EXCEPTIONS READY\r\n");
    const bool ps2_input_ready = input_init();
    (void)xhci_init();
    if (wifi_available()) {
        if (wifi_start()) {
            serial_write("WIFI: registered radio driver started\\r\\n");
        } else {
            serial_write("WIFI: registered radio driver failed to start\\r\\n");
        }
    }
    const bool usb_keyboard_ready = xhci_keyboard_ready();
    const bool usb_mouse_ready = xhci_mouse_ready();
    if (!ps2_input_ready && !usb_keyboard_ready) {
        serial_write("INPUT: no PS/2 or USB HID keyboard detected; keyboard input unavailable\r\n");
    }
    if (usb_keyboard_ready)
        serial_write("INPUT: USB HID boot keyboard available\r\n");
    if (usb_mouse_ready)
        serial_write("INPUT: USB HID boot mouse available\r\n");
    process_init();
    if (!usermode_run_selftest()) {
        serial_write("ERROR: CPL3 user-mode self-test failed\r\n");
        halt_forever();
    }
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
        if (wifi_available()) wifi_poll();
        xhci_poll();
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
