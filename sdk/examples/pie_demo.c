#include "nexus/app.h"

#include <stdint.h>

/*
 * The pointer initializer deliberately creates an R_X86_64_RELATIVE dynamic
 * relocation. The constrained Nexus static-PIE loader must relocate it before
 * entering _start.
 */
static volatile uint64_t pie_value = 0x4E45585553504945ULL;
static volatile uint64_t * volatile pie_pointer = &pie_value;

void _start(void) {
    if (nexus_app_abi_version() != NEXUS_APP_ABI_VERSION)
        nexus_app_exit(2U);

    const volatile uint64_t *pointer = pie_pointer;
    if (pointer != &pie_value || *pointer != 0x4E45585553504945ULL)
        nexus_app_exit(3U);

    static const char message[] =
        "Nexus static PIE relocation test passed.\n";
    (void)nexus_app_write(1U, message, sizeof(message) - 1U);
    nexus_app_exit(0U);
}
