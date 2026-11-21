#include "usermode.h"

#include "memory.h"
#include "gdt.h"

#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096U
#define PAGE_PRESENT 0x001ULL
#define PAGE_WRITABLE 0x002ULL
#define PAGE_USER 0x004ULL
#define PAGE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL
#define CR4_LA57 (1ULL << 12)
#define USER_CODE_VA 0x00400000ULL
#define USER_STACK_VA 0x00800000ULL
#define USER_STACK_TOP (USER_STACK_VA + PAGE_SIZE)
#define USER_EXIT_STATUS 42U

uint64_t usermode_saved_cr3;
uintptr_t usermode_saved_rsp;
volatile uint64_t usermode_exit_status = UINT64_MAX;

extern void nexus_enter_user(uint64_t root_physical,
                             uint64_t entry_point,
                             uint64_t stack_pointer,
                             uint64_t kernel_stack_top);

static uint8_t usermode_kernel_stack[16384U] __attribute__((aligned(16)));

static bool allocate_page(uint64_t *physical, volatile uint64_t **virtual_page) {
    if (physical == NULL || virtual_page == NULL ||
        !memory_alloc_page(physical)) return false;
    volatile uint64_t *page =
        (volatile uint64_t *)memory_phys_to_virt(*physical);
    if (page == NULL) return false;
    for (size_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i)
        page[i] = 0;
    *virtual_page = page;
    return true;
}

static bool allocate_user_page(uint64_t *physical, volatile uint8_t **virtual_page) {
    if (physical == NULL || virtual_page == NULL ||
        !memory_alloc_page(physical)) return false;
    volatile uint8_t *page =
        (volatile uint8_t *)memory_phys_to_virt(*physical);
    if (page == NULL) return false;
    for (size_t i = 0; i < PAGE_SIZE; ++i) page[i] = 0;
    *virtual_page = page;
    return true;
}

bool usermode_run_selftest(void) {
    uint64_t kernel_cr3;
    uint64_t cr4;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(kernel_cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));

    const uint64_t kernel_root_physical = kernel_cr3 & PAGE_ADDRESS_MASK;
    volatile uint64_t *kernel_root =
        (volatile uint64_t *)memory_phys_to_virt(kernel_root_physical);
    if (kernel_root == NULL) return false;

    uint64_t user_root_physical = 0;
    uint64_t pml4_physical = 0;
    uint64_t pdpt_physical = 0;
    uint64_t pd_physical = 0;
    uint64_t code_pt_physical = 0;
    uint64_t stack_pt_physical = 0;
    uint64_t code_physical = 0;
    uint64_t stack_physical = 0;
    volatile uint64_t *user_root = NULL;
    volatile uint64_t *user_pml4 = NULL;
    volatile uint64_t *pdpt = NULL;
    volatile uint64_t *pd = NULL;
    volatile uint64_t *code_pt = NULL;
    volatile uint64_t *stack_pt = NULL;
    volatile uint8_t *code_page = NULL;
    volatile uint8_t *stack_page = NULL;

    if (!allocate_page(&user_root_physical, &user_root) ||
        !allocate_page(&pdpt_physical, &pdpt) ||
        !allocate_page(&pd_physical, &pd) ||
        !allocate_page(&code_pt_physical, &code_pt) ||
        !allocate_page(&stack_pt_physical, &stack_pt) ||
        !allocate_user_page(&code_physical, &code_page) ||
        !allocate_user_page(&stack_physical, &stack_page)) {
        return false;
    }

    for (size_t i = 0; i < 512U; ++i) user_root[i] = kernel_root[i];

    if ((cr4 & CR4_LA57) != 0U) {
        if (!allocate_page(&pml4_physical, &user_pml4)) return false;
        user_root[0] = pml4_physical | PAGE_PRESENT |
                       PAGE_WRITABLE | PAGE_USER;
        user_pml4[0] = pdpt_physical | PAGE_PRESENT |
                       PAGE_WRITABLE | PAGE_USER;
    } else {
        user_root[0] = pdpt_physical | PAGE_PRESENT |
                       PAGE_WRITABLE | PAGE_USER;
    }

    pdpt[0] = pd_physical | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    pd[2] = code_pt_physical | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    pd[4] = stack_pt_physical | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    code_pt[0] = code_physical | PAGE_PRESENT | PAGE_USER;
    stack_pt[0] = stack_physical | PAGE_PRESENT |
                  PAGE_WRITABLE | PAGE_USER;

    /*
     * The user program calls int 0x80 with syscall 1 (exit), status 42.
     * Its code page is user-readable/executable and not user-writable.
     */
    static const uint8_t test_program[] = {
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,
        0x48, 0xC7, 0xC7, 0x2A, 0x00, 0x00, 0x00,
        0xCD, 0x80,
        0xEB, 0xFE
    };
    for (size_t i = 0; i < sizeof(test_program); ++i)
        code_page[i] = test_program[i];
    (void)stack_page;

    const uint64_t kernel_stack_top = (uint64_t)(uintptr_t)
        (usermode_kernel_stack + sizeof(usermode_kernel_stack));
    if (!gdt_set_kernel_stack(kernel_stack_top)) return false;

    usermode_exit_status = UINT64_MAX;
    nexus_enter_user(user_root_physical, USER_CODE_VA, USER_STACK_TOP,
                     kernel_stack_top);
    return usermode_exit_status == USER_EXIT_STATUS;
}
