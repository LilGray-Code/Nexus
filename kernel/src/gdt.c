#include "gdt.h"

#include <stddef.h>
#include <stdint.h>

#define GDT_ENTRY_COUNT 7U
#define KERNEL_CODE_SELECTOR 0x08U
#define KERNEL_DATA_SELECTOR 0x10U
#define USER_DATA_SELECTOR 0x1BU
#define USER_CODE_SELECTOR 0x23U
#define TSS_SELECTOR 0x28U
#define PRIVILEGE_STACK_SIZE 16384U

struct gdt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

_Static_assert(sizeof(struct tss64) == 104U,
               "x86-64 TSS must be 104 bytes");

struct gdt_cpu_state {
    uint64_t entries[GDT_ENTRY_COUNT] __attribute__((aligned(16)));
    struct tss64 tss __attribute__((aligned(16)));
    uint8_t privilege_stack[PRIVILEGE_STACK_SIZE]
        __attribute__((aligned(16)));
    bool initialized;
};

static struct gdt_cpu_state cpu_states[NEXUS_GDT_MAX_CPUS]
    __attribute__((aligned(64)));
static bool bsp_initialized;

extern void nexus_gdt_load(const struct gdt_pointer *pointer,
                           uint16_t tss_selector);

static uint64_t make_tss_descriptor(uint64_t base, uint32_t limit) {
    return ((uint64_t)(limit & 0xFFFFU)) |
           ((base & 0xFFFFFFULL) << 16) |
           (0x89ULL << 40) |
           ((uint64_t)((limit >> 16) & 0x0FU) << 48) |
           (((base >> 24) & 0xFFULL) << 56);
}

static bool gdt_init_cpu(uint32_t slot) {
    if (slot >= NEXUS_GDT_MAX_CPUS) return false;
    struct gdt_cpu_state *state = &cpu_states[slot];
    if (state->initialized) return false;

    for (size_t i = 0U; i < GDT_ENTRY_COUNT; ++i)
        state->entries[i] = 0U;

    /* Flat long-mode kernel code/data and user data/code descriptors. */
    state->entries[1] = 0x00AF9A000000FFFFULL;
    state->entries[2] = 0x00CF92000000FFFFULL;
    state->entries[3] = 0x00CFF2000000FFFFULL;
    state->entries[4] = 0x00AFFA000000FFFFULL;

    state->tss = (struct tss64){0};
    state->tss.rsp0 = (uint64_t)(uintptr_t)
        (state->privilege_stack + sizeof(state->privilege_stack));
    state->tss.iomap_base = (uint16_t)sizeof(state->tss);
    const uint64_t tss_base = (uint64_t)(uintptr_t)&state->tss;
    state->entries[5] =
        make_tss_descriptor(tss_base, (uint32_t)sizeof(state->tss) - 1U);
    state->entries[6] = tss_base >> 32;

    const struct gdt_pointer pointer = {
        .limit = (uint16_t)(sizeof(state->entries) - 1U),
        .base = (uint64_t)(uintptr_t)state->entries
    };
    nexus_gdt_load(&pointer, TSS_SELECTOR);
    state->initialized = true;
    return true;
}

bool gdt_init(void) {
    bsp_initialized = false;
    if (!gdt_init_cpu(0U)) return false;
    bsp_initialized = true;
    return true;
}

bool gdt_init_ap(uint32_t cpu_slot) {
    /* Slot zero belongs to the BSP, which already loaded its own TSS. */
    if (cpu_slot == 0U) return false;
    return gdt_init_cpu(cpu_slot);
}

uint64_t gdt_get_kernel_stack(void) {
    if (!bsp_initialized) return 0U;
    __asm__ volatile ("" : : : "memory");
    return cpu_states[0].tss.rsp0;
}

bool gdt_set_kernel_stack(uint64_t stack_top) {
    if (!bsp_initialized || stack_top == 0U || (stack_top & 0x0FU) != 0U)
        return false;
    cpu_states[0].tss.rsp0 = stack_top;
    __asm__ volatile ("" : : : "memory");
    return true;
}
