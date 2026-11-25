#include "smp.h"

#include <limine.h>

#include "gdt.h"
#include "interrupts.h"
#include "paging.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define IA32_APIC_BASE_MSR 0x1BU
#define IA32_X2APIC_ID 0x802U
#define IA32_X2APIC_TPR 0x808U
#define IA32_X2APIC_EOI 0x80BU
#define IA32_X2APIC_SVR 0x80FU
#define IA32_X2APIC_ICR 0x830U
#define IA32_X2APIC_LVT_LINT0 0x835U
#define APIC_BASE_ENABLE (1ULL << 11)
#define APIC_BASE_X2APIC (1ULL << 10)
#define APIC_PHYSICAL_MASK 0x0000000FFFFFF000ULL
#define APIC_ID_REGISTER 0x020U
#define APIC_EOI_REGISTER 0x0B0U
#define APIC_SVR_REGISTER 0x0F0U
#define APIC_LVT_LINT0_REGISTER 0x350U
#define APIC_ICR_LOW_REGISTER 0x300U
#define APIC_ICR_HIGH_REGISTER 0x310U
#define APIC_DELIVERY_STATUS (1U << 12)
#define APIC_LVT_MASKED (1U << 16)
#define APIC_SPURIOUS_VECTOR 0xFFU
#define SMP_WAIT_SPINS 250000000ULL

static uint32_t lapic_ids[NEXUS_SMP_MAX_CPUS];
static volatile uint32_t cpu_online[NEXUS_SMP_MAX_CPUS];
static uint32_t expected_cpu_count = 1U;
static volatile uint32_t online_cpu_count = 1U;
static bool smp_initialized;
static bool x2apic_enabled;
static uint64_t lapic_physical;
static volatile uint32_t *lapic_mmio;

static volatile uint32_t work_active;
static volatile uint32_t work_next;
static volatile uint32_t work_completed;
static uint32_t work_item_count;
static nexus_smp_task_fn work_task;
static void *work_context;

static uint64_t read_msr(uint32_t msr) {
    uint32_t low = 0U, high = 0U;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile ("wrmsr" : :
        "c"(msr), "a"((uint32_t)value),
        "d"((uint32_t)(value >> 32)) : "memory");
}

static void local_apic_eoi(void) {
    if (x2apic_enabled) {
        write_msr(IA32_X2APIC_EOI, 0U);
        return;
    }
    if (lapic_mmio == NULL) {
        volatile uint32_t *mapped = NULL;
        if (!paging_map_mmio_page(lapic_physical, &mapped) || mapped == NULL)
            return;
        lapic_mmio = mapped;
    }
    lapic_mmio[APIC_EOI_REGISTER / 4U] = 0U;
    __asm__ volatile ("mfence" : : : "memory");
}

static bool local_apic_init_current_cpu(void) {
    uint64_t base = read_msr(IA32_APIC_BASE_MSR);
    base |= APIC_BASE_ENABLE;
    if (x2apic_enabled) base |= APIC_BASE_X2APIC;
    else base &= ~APIC_BASE_X2APIC;
    write_msr(IA32_APIC_BASE_MSR, base);

    if (x2apic_enabled) {
        write_msr(IA32_X2APIC_TPR, 0U);
        write_msr(IA32_X2APIC_SVR,
                  (read_msr(IA32_X2APIC_SVR) & ~0xFFULL) |
                  APIC_SPURIOUS_VECTOR | (1ULL << 8));
        write_msr(IA32_X2APIC_LVT_LINT0, APIC_LVT_MASKED);
        return true;
    }

    const uint64_t physical = base & APIC_PHYSICAL_MASK;
    volatile uint32_t *mapped = NULL;
    if (!paging_map_mmio_page(physical, &mapped) || mapped == NULL)
        return false;
    lapic_physical = physical;
    lapic_mmio = mapped;
    lapic_mmio[0x80U / 4U] = 0U;
    lapic_mmio[APIC_LVT_LINT0_REGISTER / 4U] = APIC_LVT_MASKED;
    uint32_t svr = lapic_mmio[APIC_SVR_REGISTER / 4U];
    lapic_mmio[APIC_SVR_REGISTER / 4U] =
        (svr & ~0xFFU) | APIC_SPURIOUS_VECTOR | (1U << 8);
    __asm__ volatile ("mfence" : : : "memory");
    return true;
}

static uint32_t local_apic_id(void) {
    if (x2apic_enabled) return (uint32_t)read_msr(IA32_X2APIC_ID);
    if (lapic_mmio == NULL) {
        volatile uint32_t *mapped = NULL;
        if (!paging_map_mmio_page(lapic_physical, &mapped) || mapped == NULL)
            return UINT32_MAX;
        lapic_mmio = mapped;
    }
    return lapic_mmio[APIC_ID_REGISTER / 4U] >> 24;
}

static uint32_t current_cpu_slot(void) {
    const uint32_t id = local_apic_id();
    if (id == UINT32_MAX) return UINT32_MAX;
    for (uint32_t slot = 0U; slot < expected_cpu_count; ++slot) {
        if (lapic_ids[slot] == id) return slot;
    }
    return UINT32_MAX;
}

static bool send_fixed_ipi(uint32_t destination_id) {
    if (x2apic_enabled) {
        const uint64_t command =
            ((uint64_t)destination_id << 32) | NEXUS_SMP_IPI_VECTOR;
        write_msr(IA32_X2APIC_ICR, command);
        for (uint64_t spin = 0U; spin < 1000000U; ++spin) {
            if ((read_msr(IA32_X2APIC_ICR) & APIC_DELIVERY_STATUS) == 0U)
                return true;
            __asm__ volatile ("pause" ::: "memory");
        }
        return false;
    }

    if (destination_id > 0xFFU) return false;
    if (lapic_mmio == NULL) {
        volatile uint32_t *mapped = NULL;
        if (!paging_map_mmio_page(lapic_physical, &mapped) || mapped == NULL)
            return false;
        lapic_mmio = mapped;
    }
    for (uint64_t spin = 0U; spin < 1000000U; ++spin) {
        if ((lapic_mmio[APIC_ICR_LOW_REGISTER / 4U] &
             APIC_DELIVERY_STATUS) == 0U) break;
        __asm__ volatile ("pause" ::: "memory");
    }
    if ((lapic_mmio[APIC_ICR_LOW_REGISTER / 4U] &
         APIC_DELIVERY_STATUS) != 0U) return false;
    lapic_mmio[APIC_ICR_HIGH_REGISTER / 4U] = destination_id << 24;
    __asm__ volatile ("mfence" : : : "memory");
    lapic_mmio[APIC_ICR_LOW_REGISTER / 4U] = NEXUS_SMP_IPI_VECTOR;
    __asm__ volatile ("mfence" : : : "memory");
    for (uint64_t spin = 0U; spin < 1000000U; ++spin) {
        if ((lapic_mmio[APIC_ICR_LOW_REGISTER / 4U] &
             APIC_DELIVERY_STATUS) == 0U) return true;
        __asm__ volatile ("pause" ::: "memory");
    }
    return false;
}

static void run_claimed_work(uint32_t cpu_slot) {
    for (;;) {
        const uint32_t item =
            __atomic_fetch_add(&work_next, 1U, __ATOMIC_RELAXED);
        if (item >= work_item_count) break;
        work_task(cpu_slot, item, work_context);
    }
}

void nexus_smp_ipi_interrupt(void) {
    if (__atomic_load_n(&work_active, __ATOMIC_ACQUIRE) != 0U) {
        const uint32_t slot = current_cpu_slot();
        if (slot != UINT32_MAX && slot != 0U) {
            run_claimed_work(slot);
            __atomic_fetch_add(&work_completed, 1U, __ATOMIC_RELEASE);
        }
    }
    local_apic_eoi();
}

__attribute__((noreturn))
static void smp_ap_entry(struct limine_mp_info *info) {
    __asm__ volatile ("cli; cld" : : : "memory");
    if (info == NULL) {
        for (;;) __asm__ volatile ("cli; hlt" ::: "memory");
    }

    const uint32_t slot =
        (uint32_t)__atomic_load_n(&info->extra_argument, __ATOMIC_ACQUIRE);
    if (slot == 0U || slot >= expected_cpu_count ||
        !gdt_init_ap(slot) || !idt_load_current_cpu() ||
        !local_apic_init_current_cpu() ||
        local_apic_id() != info->lapic_id) {
        for (;;) __asm__ volatile ("cli; hlt" ::: "memory");
    }

    cpu_online[slot] = 1U;
    __atomic_add_fetch(&online_cpu_count, 1U, __ATOMIC_RELEASE);
    for (;;) {
        __asm__ volatile ("sti; hlt; cli" : : : "memory");
    }
}

bool nexus_smp_init(struct limine_mp_response *response) {
    smp_initialized = false;
    expected_cpu_count = 1U;
    online_cpu_count = 1U;
    cpu_online[0] = 1U;
    lapic_ids[0] = (uint32_t)read_msr(IA32_APIC_BASE_MSR);
    work_active = 0U;
    work_next = 0U;
    work_completed = 0U;

    if (response == NULL || response->cpu_count == 0U ||
        response->cpus == NULL) {
        const uint64_t apic_base = read_msr(IA32_APIC_BASE_MSR);
        x2apic_enabled = (apic_base & APIC_BASE_X2APIC) != 0U;
        (void)local_apic_init_current_cpu();
        lapic_ids[0] = local_apic_id();
        smp_initialized = true;
        serial_write("SMP: Limine MP response unavailable; single-CPU fallback\r\n");
        return true;
    }

    const uint64_t reported = response->cpu_count;
    const uint32_t capacity = reported > NEXUS_SMP_MAX_CPUS
        ? NEXUS_SMP_MAX_CPUS : (uint32_t)reported;
    uint32_t bsp_index = UINT32_MAX;
    for (uint32_t i = 0U; i < capacity; ++i) {
        if (response->cpus[i] == NULL) return false;
        if (response->cpus[i]->lapic_id == response->bsp_lapic_id)
            bsp_index = i;
    }
    if (bsp_index == UINT32_MAX) return false;

    const uint64_t apic_base = read_msr(IA32_APIC_BASE_MSR);
    x2apic_enabled = (apic_base & APIC_BASE_X2APIC) != 0U;
    if (!local_apic_init_current_cpu()) {
        lapic_ids[0] = response->bsp_lapic_id;
        smp_initialized = true;
        serial_write("SMP: BSP local APIC setup failed; single-CPU fallback\r\n");
        return true;
    }

    uint32_t next_slot = 1U;
    lapic_ids[0] = response->bsp_lapic_id;
    for (uint32_t i = 0U; i < capacity; ++i) {
        if (i == bsp_index) continue;
        struct limine_mp_info *info = response->cpus[i];
        const uint32_t slot = next_slot++;
        lapic_ids[slot] = info->lapic_id;
        info->extra_argument = slot;
        __atomic_store_n(&info->goto_address, smp_ap_entry, __ATOMIC_RELEASE);
    }

    expected_cpu_count = next_slot;
    for (uint64_t spin = 0U; spin < SMP_WAIT_SPINS; ++spin) {
        if (__atomic_load_n(&online_cpu_count, __ATOMIC_ACQUIRE) >=
            expected_cpu_count) break;
        __asm__ volatile ("pause" ::: "memory");
    }

    const uint32_t online =
        __atomic_load_n(&online_cpu_count, __ATOMIC_ACQUIRE);
    smp_initialized = true;
    serial_write("NEXUS SMP ONLINE CPU COUNT=");
    serial_write_hex(online);
    serial_write("\r\n");
    if (online != expected_cpu_count) {
        serial_write("SMP: one or more APs did not finish initialization\r\n");
    }
    return true;
}

uint32_t nexus_smp_online_cpu_count(void) {
    return __atomic_load_n(&online_cpu_count, __ATOMIC_ACQUIRE);
}

bool nexus_smp_parallel_for(uint32_t item_count,
                            nexus_smp_task_fn task,
                            void *context) {
    if (!smp_initialized || task == NULL) return false;
    if (item_count == 0U) return true;
    if (__atomic_load_n(&work_active, __ATOMIC_ACQUIRE) != 0U) return false;
    const uint32_t online = nexus_smp_online_cpu_count();
    /* Single-CPU fallback can run without an accessible local APIC. */
    if (online > 1U && current_cpu_slot() != 0U) return false;

    work_task = task;
    work_context = context;
    work_item_count = item_count;
    __atomic_store_n(&work_next, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&work_completed, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&work_active, 1U, __ATOMIC_RELEASE);

    uint32_t remote_count = 0U;
    for (uint32_t slot = 1U; slot < expected_cpu_count; ++slot) {
        if (__atomic_load_n(&cpu_online[slot], __ATOMIC_ACQUIRE) == 0U)
            continue;
        if (!send_fixed_ipi(lapic_ids[slot])) {
            __atomic_store_n(&work_active, 0U, __ATOMIC_RELEASE);
            return false;
        }
        ++remote_count;
    }

    run_claimed_work(0U);
    for (uint64_t spin = 0U; spin < SMP_WAIT_SPINS; ++spin) {
        if (__atomic_load_n(&work_completed, __ATOMIC_ACQUIRE) >=
            remote_count) {
            __atomic_store_n(&work_active, 0U, __ATOMIC_RELEASE);
            return true;
        }
        __asm__ volatile ("pause" ::: "memory");
    }

    __atomic_store_n(&work_active, 0U, __ATOMIC_RELEASE);
    serial_write("SMP: parallel work dispatch timed out online=");
    serial_write_hex(online);
    serial_write(" completed=");
    serial_write_hex(__atomic_load_n(&work_completed, __ATOMIC_ACQUIRE));
    serial_write("\r\n");
    return false;
}
