#include "fpu.h"

#include "cpu.h"

#include <stdint.h>

#define CR0_MP (1ULL << 1)
#define CR0_EM (1ULL << 2)
#define CR0_TS (1ULL << 3)
#define CR0_NE (1ULL << 5)
#define CR4_OSFXSR (1ULL << 9)
#define CR4_OSXMMEXCPT (1ULL << 10)
#define CR4_OSXSAVE (1ULL << 18)
#define FPU_STATE_BYTES 512U

static uint8_t saved_kernel_fpu_state[FPU_STATE_BYTES]
    __attribute__((aligned(16)));
static bool fpu_initialized;
static bool user_fpu_context_active;
static const uint32_t default_mxcsr = 0x1F80U;

static void reset_user_fpu_state(void) {
    /*
     * The kernel is built with -mgeneral-regs-only and does not use x87/SSE.
     * Applications receive a clean x87 control word, default masked MXCSR,
     * and cleared XMM registers. AVX is intentionally not enabled.
     */
    __asm__ volatile (
        "fninit\n\t"
        "ldmxcsr %0\n\t"
        "pxor %%xmm0, %%xmm0\n\t"
        "pxor %%xmm1, %%xmm1\n\t"
        "pxor %%xmm2, %%xmm2\n\t"
        "pxor %%xmm3, %%xmm3\n\t"
        "pxor %%xmm4, %%xmm4\n\t"
        "pxor %%xmm5, %%xmm5\n\t"
        "pxor %%xmm6, %%xmm6\n\t"
        "pxor %%xmm7, %%xmm7\n\t"
        "pxor %%xmm8, %%xmm8\n\t"
        "pxor %%xmm9, %%xmm9\n\t"
        "pxor %%xmm10, %%xmm10\n\t"
        "pxor %%xmm11, %%xmm11\n\t"
        "pxor %%xmm12, %%xmm12\n\t"
        "pxor %%xmm13, %%xmm13\n\t"
        "pxor %%xmm14, %%xmm14\n\t"
        "pxor %%xmm15, %%xmm15\n\t"
        : : "m"(default_mxcsr) : "memory");
}

bool nexus_fpu_init(void) {
    const uint64_t required = NEXUS_CPU_FEATURE_FPU |
        NEXUS_CPU_FEATURE_FXSR | NEXUS_CPU_FEATURE_SSE |
        NEXUS_CPU_FEATURE_SSE2;
    fpu_initialized = false;
    user_fpu_context_active = false;
    if (!nexus_cpu_has_feature(required)) return false;

    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_MP | CR0_NE;
    cr0 &= ~(CR0_EM | CR0_TS);
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");

    uint64_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_OSFXSR | CR4_OSXMMEXCPT;
    cr4 &= ~CR4_OSXSAVE;
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");

    nexus_cpu_disable_avx();
    reset_user_fpu_state();
    fpu_initialized = true;
    return true;
}

bool nexus_fpu_available(void) {
    return fpu_initialized;
}

bool nexus_fpu_user_begin(void) {
    if (!fpu_initialized || user_fpu_context_active) return false;
    __asm__ volatile ("fxsave64 %0" : "=m"(saved_kernel_fpu_state) : : "memory");
    reset_user_fpu_state();
    user_fpu_context_active = true;
    return true;
}

bool nexus_fpu_user_end(void) {
    if (!user_fpu_context_active) return false;
    __asm__ volatile ("fxrstor64 %0" : : "m"(saved_kernel_fpu_state) : "memory");
    user_fpu_context_active = false;
    return true;
}
