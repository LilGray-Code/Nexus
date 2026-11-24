#include "random.h"

#include <stddef.h>
#include <stdint.h>

#define CPUID_LEAF_FEATURES 1U
#define CPUID_RDRAND_ECX_BIT (1U << 30)
#define RDRAND_RETRY_LIMIT 10U

static bool cpu_has_rdrand(void) {
    uint32_t eax = 0U, ebx = 0U, ecx = 0U, edx = 0U;
    __asm__ volatile ("cpuid"
                      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      : "a"(0U), "c"(0U));
    if (eax < CPUID_LEAF_FEATURES) return false;

    __asm__ volatile ("cpuid"
                      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      : "a"(CPUID_LEAF_FEATURES), "c"(0U));
    return (ecx & CPUID_RDRAND_ECX_BIT) != 0U;
}

static bool hardware_random_u64(uint64_t *value) {
    if (value == NULL) return false;
    for (unsigned attempt = 0U; attempt < RDRAND_RETRY_LIMIT; ++attempt) {
        uint64_t candidate = 0U;
        unsigned char success = 0U;
        __asm__ volatile ("rdrand %0; setc %1"
                          : "=&r"(candidate), "=qm"(success)
                          :
                          : "cc");
        if (success != 0U) {
            *value = candidate;
            return true;
        }
        __asm__ volatile ("pause" ::: "memory");
    }
    return false;
}

bool nexus_rng_available(void) {
    return cpu_has_rdrand();
}

bool nexus_rng_fill(void *output, size_t length) {
    if (length == 0U) return true;
    if (output == NULL || !cpu_has_rdrand()) {
        if (output != NULL) {
            uint8_t *bytes = (uint8_t *)output;
            for (size_t i = 0U; i < length; ++i) bytes[i] = 0U;
        }
        return false;
    }

    uint8_t *bytes = (uint8_t *)output;
    size_t offset = 0U;
    while (offset < length) {
        uint64_t random_word = 0U;
        if (!hardware_random_u64(&random_word)) {
            /* Never leave callers with a partially generated secret. */
            for (size_t i = 0U; i < length; ++i) bytes[i] = 0U;
            return false;
        }

        size_t amount = length - offset;
        if (amount > sizeof(random_word)) amount = sizeof(random_word);
        const uint8_t *word_bytes = (const uint8_t *)&random_word;
        for (size_t i = 0U; i < amount; ++i)
            bytes[offset + i] = word_bytes[i];

        /* Do not keep an extra copy of generated key material on the stack. */
        volatile uint64_t *wipe_word = &random_word;
        *wipe_word = 0U;
        offset += amount;
    }
    return true;
}

bool nexus_rng_u32(uint32_t *value) {
    if (value == NULL) return false;
    uint32_t candidate = 0U;
    if (!nexus_rng_fill(&candidate, sizeof(candidate))) {
        *value = 0U;
        return false;
    }
    *value = candidate;
    volatile uint32_t *wipe_candidate = &candidate;
    *wipe_candidate = 0U;
    return true;
}
