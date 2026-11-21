#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

#include "acpi.h"
#include "blockdev.h"
#include "desktop.h"
#include "elf64.h"
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
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

static volatile uint32_t scheduler_count_a;
static volatile uint32_t scheduler_count_b;
static volatile uint8_t scheduler_trace[6];
static volatile uint32_t scheduler_trace_count;

static void scheduler_worker_a(void *argument) {
    (void)argument;
    for (uint32_t i = 0; i < 3; ++i) {
        scheduler_count_a++;
        if (scheduler_trace_count < 6)
            scheduler_trace[scheduler_trace_count++] = 1;
        scheduler_yield();
    }
}

static void scheduler_worker_b(void *argument) {
    (void)argument;
    for (uint32_t i = 0; i < 3; ++i) {
        scheduler_count_b++;
        if (scheduler_trace_count < 6)
            scheduler_trace[scheduler_trace_count++] = 2;
        scheduler_yield();
    }
}

__attribute__((noreturn))
static void halt_forever(void) {
    for (;;) __asm__ volatile ("cli; hlt" ::: "memory");
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

    if (!gdt_init()) {
        serial_write("ERROR: Nexus GDT/TSS initialization failed\\r\\n");
        halt_forever();
    }
    serial_write("NEXUS GDT TSS READY\\r\\n");

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

    if (!ioapic_probe()) {
        serial_write("ERROR: I/O APIC read-only probe failed\r\n");
        halt_forever();
    }
    serial_write("NEXUS IOAPIC PROBE READY\r\n");

    if (!storage_probe()) {
        serial_write("WARNING: no mass-storage PCI controller discovered\r\n");
    }
    serial_write("NEXUS STORAGE DISCOVERY READY\r\n");

    timer_init(100);
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
    serial_write("K2: cooperative scheduler test passed (A-B-A-B-A-B)\r\n");

    if (framebuffer_request.response != NULL &&
        framebuffer_request.response->framebuffer_count > 0) {
        (void)desktop_show(framebuffer_request.response->framebuffers[0]);
    } else {
        serial_write("DESKTOP: no framebuffer supplied by bootloader\r\n");
    }
    serial_write("NEXUS K2 COOPERATIVE SCHEDULER READY\r\n");
    serial_write("NEXUS K2 TIMER INTERRUPT READY\r\n");
    serial_write("NEXUS K1 MEMORY+EXCEPTIONS READY\r\n");
    const bool input_ready = input_init();
    if (!input_ready) {
        serial_write("INPUT: no PS/2 devices detected; desktop remains keyboard/mouse inactive\\r\\n");
    }
    process_init();
    if (!usermode_run_selftest()) {
        serial_write("ERROR: CPL3 user-mode self-test failed\\r\\n");
        halt_forever();
    }
#ifdef NEXUS_STORAGE_SELFTEST
    const enum nexus_desktop_process_test_result file_test =
        desktop_run_process_selftest();
    if (file_test == NEXUS_DESKTOP_PROCESS_TEST_FAILED) {
        serial_write("ERROR: FAT32 ELF file execution self-test failed\\r\\n");
        halt_forever();
    } else if (file_test == NEXUS_DESKTOP_PROCESS_TEST_PASSED) {
        serial_write("NEXUS FILE PROCESS SELFTEST READY\\r\\n");
    } else {
        serial_write("NEXUS FILE PROCESS SELFTEST SKIPPED\\r\\n");
    }
#endif
    serial_write("NEXUS PROCESS MANAGER READY\r\n");
    serial_write("NEXUS ELF64 USER LOADER READY\r\n");
    serial_write("NEXUS USER MODE SELFTEST READY\\r\\n");
    serial_write("NEXUS SYSCALL ABI READY\\r\\n");
    serial_write("NEXUS K0 BOOT COMPLETE\\r\\n");

    /* Timer and input IRQs wake the kernel while the event queue is drained. */
    __asm__ volatile ("sti" : : : "memory");
    for (;;) {
        struct nexus_input_event event;
        while (input_pop_event(&event)) {
            desktop_handle_input(&event);
            if (event.type == NEXUS_INPUT_KEYBOARD) {
                serial_write("INPUT: keyboard scancode=");
                serial_write_hex(event.scancode);
                serial_write("\\r\\n");
            } else if (event.type == NEXUS_INPUT_MOUSE) {
                serial_write("INPUT: mouse dx=");
                serial_write_hex((uint16_t)event.mouse_dx);
                serial_write(" dy=");
                serial_write_hex((uint16_t)event.mouse_dy);
                serial_write(" buttons=");
                serial_write_hex(event.mouse_buttons);
                serial_write("\\r\\n");
            }
        }
        __asm__ volatile ("sti; hlt" : : : "memory");
    }
}
