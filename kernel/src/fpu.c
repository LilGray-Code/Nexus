#include "fpu.h"
#include "cpu.h"
#include <stddef.h>
#include <stddef.h>
#include <stdint.h>

#define CR0_MP (1ULL << 1)
#define CR0_EM (1ULL << 2)
#define CR0_TS (1ULL << 3)
#define CR0_NE (1ULL << 5)
#define CR4_OSFXSR (1ULL << 9)
#define CR4_OSXMMEXCPT (1ULL << 10)
#define CR4_OSXSAVE (1ULL << 18)
#define FPU_LEGACY_STATE_BYTES 512U
#define FPU_XSAVE_STATE_BYTES 1024U
#define XSAVE_AVX_MASK 0x7U

static uint8_t saved_kernel_fpu_state[FPU_XSAVE_STATE_BYTES]
    __attribute__((aligned(64)));
static bool fpu_initialized;
static bool avx_state_enabled;
static bool user_fpu_context_active;
static const uint32_t default_mxcsr = 0x1F80U;

static void cpuid(uint32_t leaf, uint32_t subleaf,
                  uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx) {
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(subleaf));
    if (eax != NULL) *eax = a;
    if (ebx != NULL) *ebx = b;
    if (ecx != NULL) *ecx = c;
    if (edx != NULL) *edx = d;
}

static uint64_t read_xcr0(void) {
    uint32_t low, high;
    __asm__ volatile ("xgetbv"
        : "=a"(low), "=d"(high)
        : "c"(0U));
    return ((uint64_t)high << 32) | low;
}

static void write_xcr0(uint64_t value) {
    __asm__ volatile ("xsetbv"
        : : "c"(0U), "a"((uint32_t)value),
            "d"((uint32_t)(value >> 32)) : "memory");
}

static void disable_avx_xstate(void) {
    uint64_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    if ((cr4 & CR4_OSXSAVE) != 0U) {
        write_xcr0(0x3U);
        cr4 &= ~CR4_OSXSAVE;
        __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
    }
}

static bool enable_avx_xstate(void) {
    if (nexus_cpu_get_info()->max_basic_leaf < 0x0DU) return false;
    uint32_t ecx = 0U;
    cpuid(1U, 0U, NULL, NULL, &ecx, NULL);
    if ((ecx & (1U << 26)) == 0U ||
        (ecx & (1U << 28)) == 0U) return false;

    uint32_t supported_low = 0U, supported_high = 0U;
    cpuid(0x0DU, 0U, &supported_low, NULL, NULL, &supported_high);
    const uint64_t supported_mask =
        ((uint64_t)supported_high << 32) | supported_low;
    if ((supported_mask & XSAVE_AVX_MASK) != XSAVE_AVX_MASK) return false;

    uint64_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_OSXSAVE;
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
    uint64_t verified_cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(verified_cr4));
    if ((verified_cr4 & CR4_OSXSAVE) == 0U) return false;
    write_xcr0(XSAVE_AVX_MASK);
    if ((read_xcr0() & XSAVE_AVX_MASK) != XSAVE_AVX_MASK) {
        disable_avx_xstate();
        return false;
    }

    uint32_t state_bytes = 0U;
    cpuid(0x0DU, 0U, NULL, &state_bytes, NULL, NULL);
    if (state_bytes < FPU_LEGACY_STATE_BYTES ||
        state_bytes > FPU_XSAVE_STATE_BYTES) {
        disable_avx_xstate();
        return false;
    }

    nexus_cpu_enable_avx_features();
    if (!nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX)) {
        disable_avx_xstate();
        return false;
    }
    avx_state_enabled = true;
    return true;
}

static void reset_user_fpu_state(void) {
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
    if (avx_state_enabled)
        __asm__ volatile (".byte 0xC5, 0xFC, 0x77" : : : "memory");
}

static void save_kernel_fpu_state(void) {
    if (avx_state_enabled) {
        __asm__ volatile ("xsave64 %0"
            : "=m"(saved_kernel_fpu_state)
            : "a"(XSAVE_AVX_MASK), "d"(0U) : "memory");
    } else {
        __asm__ volatile ("fxsave64 %0"
            : "=m"(saved_kernel_fpu_state) : : "memory");
    }
}

static void restore_kernel_fpu_state(void) {
    if (avx_state_enabled) {
        __asm__ volatile ("xrstor64 %0"
            : : "m"(saved_kernel_fpu_state),
                "a"(XSAVE_AVX_MASK), "d"(0U) : "memory");
    } else {
        __asm__ volatile ("fxrstor64 %0"
            : : "m"(saved_kernel_fpu_state) : "memory");
    }
}

bool nexus_fpu_init(void) {
    const uint64_t required = NEXUS_CPU_FEATURE_FPU |
        NEXUS_CPU_FEATURE_FXSR | NEXUS_CPU_FEATURE_SSE |
        NEXUS_CPU_FEATURE_SSE2;
    fpu_initialized = false;
    avx_state_enabled = false;
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

    if (!enable_avx_xstate()) nexus_cpu_disable_avx();
    reset_user_fpu_state();
    fpu_initialized = true;
    return true;
}

bool nexus_fpu_available(void) { return fpu_initialized; }
bool nexus_fpu_avx_available(void) {
    return fpu_initialized && avx_state_enabled;
}

bool nexus_fpu_user_begin(void) {
    if (!fpu_initialized || user_fpu_context_active) return false;
    save_kernel_fpu_state();
    reset_user_fpu_state();
    user_fpu_context_active = true;
    return true;
}

bool nexus_fpu_user_end(void) {
    if (!user_fpu_context_active) return false;
    restore_kernel_fpu_state();
    user_fpu_context_active = false;
    return true;
}
