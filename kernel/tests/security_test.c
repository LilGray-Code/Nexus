#include "security.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    nexus_security_init();
    assert(nexus_security_current_pid() == 0U);
    assert(nexus_security_current_capabilities() == 0U);
    assert(!nexus_security_require(NEXUS_CAP_CLOCK));

    const uint64_t granted =
        NEXUS_CAP_CONSOLE_WRITE | NEXUS_CAP_FILE_READ | NEXUS_CAP_DISPLAY;
    assert(nexus_security_capabilities_valid(granted));
    assert(!nexus_security_capabilities_valid(NEXUS_CAP_ALL | (1ULL << 40)));
    assert(nexus_security_context_enter(42U, granted));
    assert(nexus_security_current_pid() == 42U);
    assert(nexus_security_current_capabilities() == granted);
    assert(nexus_security_require(NEXUS_CAP_FILE_READ));
    assert(nexus_security_require(NEXUS_CAP_DISPLAY));
    assert(!nexus_security_require(NEXUS_CAP_FILE_WRITE));
    assert(!nexus_security_require(NEXUS_CAP_ADMIN));

    /* A second process cannot replace the identity or elevate the context. */
    assert(!nexus_security_context_enter(43U, NEXUS_CAP_ALL));
    assert(nexus_security_current_pid() == 42U);
    assert(nexus_security_current_capabilities() == granted);
    nexus_security_context_leave(43U);
    assert(nexus_security_current_pid() == 42U);
    nexus_security_context_leave(42U);
    assert(nexus_security_current_pid() == 0U);
    assert(nexus_security_current_capabilities() == 0U);
    assert(!nexus_security_require(NEXUS_CAP_CONSOLE_WRITE));

    struct nexus_security_event events[16];
    uint64_t cursor = 1U;
    const size_t count = nexus_security_audit_read(
        &cursor, events, sizeof(events) / sizeof(events[0]));
    assert(count >= 8U);
    bool saw_denied_file_write = false;
    bool saw_denied_admin = false;
    bool saw_context_reject = false;
    for (size_t i = 0U; i < count; ++i) {
        assert(events[i].sequence != 0U);
        if (events[i].type == NEXUS_SECURITY_EVENT_CAPABILITY_DENIED &&
            events[i].auxiliary == NEXUS_CAP_FILE_WRITE)
            saw_denied_file_write = true;
        if (events[i].type == NEXUS_SECURITY_EVENT_CAPABILITY_DENIED &&
            events[i].auxiliary == NEXUS_CAP_ADMIN)
            saw_denied_admin = true;
        if (events[i].type == NEXUS_SECURITY_EVENT_POLICY_REJECTED &&
            events[i].code == NEXUS_SECURITY_REASON_CONTEXT_BUSY)
            saw_context_reject = true;
    }
    assert(saw_denied_file_write && saw_denied_admin && saw_context_reject);

    /* The fixed audit ring overwrites old entries without reusing sequence IDs. */
    nexus_security_init();
    for (uint64_t i = 0U; i < NEXUS_SECURITY_AUDIT_CAPACITY + 9U; ++i) {
        nexus_security_audit(NEXUS_SECURITY_EVENT_RECOVERY, 0U, 7U, i, 0U);
    }
    struct nexus_security_event retained_events[NEXUS_SECURITY_AUDIT_CAPACITY];
    cursor = 1U;
    const size_t retained = nexus_security_audit_read(
        &cursor, retained_events,
        sizeof(retained_events) / sizeof(retained_events[0]));
    assert(retained == NEXUS_SECURITY_AUDIT_CAPACITY);
    assert(retained_events[0].sequence == 10U);
    assert(retained_events[0].detail == 9U);
    assert(retained_events[retained - 1U].sequence == 73U);
    assert(cursor == 74U);

    puts("NEXUS SECURITY POLICY TESTS PASSED");
    return 0;
}
