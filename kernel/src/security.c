#include "security.h"

#include <stdatomic.h>

static atomic_flag security_lock = ATOMIC_FLAG_INIT;
static uint32_t active_pid;
static uint64_t active_capabilities;
static struct nexus_security_event audit_ring[NEXUS_SECURITY_AUDIT_CAPACITY];
static uint64_t next_sequence = 1U;

static void lock_security(void) {
    while (atomic_flag_test_and_set_explicit(&security_lock,
                                              memory_order_acquire)) {
        __asm__ volatile ("pause" : : : "memory");
    }
}

static void unlock_security(void) {
    atomic_flag_clear_explicit(&security_lock, memory_order_release);
}

void nexus_security_init(void) {
    atomic_flag_clear_explicit(&security_lock, memory_order_release);
    lock_security();
    active_pid = 0U;
    active_capabilities = 0U;
    next_sequence = 1U;
    for (size_t i = 0U; i < NEXUS_SECURITY_AUDIT_CAPACITY; ++i)
        audit_ring[i] = (struct nexus_security_event){0};
    unlock_security();
}

bool nexus_security_capabilities_valid(uint64_t capabilities) {
    return (capabilities & ~NEXUS_CAP_ALL) == 0U;
}

void nexus_security_audit(uint16_t type, uint32_t actor_pid, uint16_t code,
                          uint64_t detail, uint64_t auxiliary) {
    lock_security();
    /*
     * Sequence exhaustion is practically unreachable, but do not wrap and
     * accidentally make stale cursors appear current.
     */
    if (next_sequence == UINT64_MAX) {
        for (size_t i = 0U; i < NEXUS_SECURITY_AUDIT_CAPACITY; ++i)
            audit_ring[i] = (struct nexus_security_event){0};
        next_sequence = 1U;
    }
    const uint64_t sequence = next_sequence++;
    const size_t slot =
        (size_t)((sequence - 1U) % NEXUS_SECURITY_AUDIT_CAPACITY);
    audit_ring[slot] = (struct nexus_security_event){
        .sequence = sequence,
        .detail = detail,
        .auxiliary = auxiliary,
        .actor_pid = actor_pid,
        .type = type,
        .code = code
    };
    unlock_security();
}

bool nexus_security_context_enter(uint32_t pid, uint64_t capabilities) {
    if (pid == 0U || !nexus_security_capabilities_valid(capabilities)) {
        nexus_security_audit(NEXUS_SECURITY_EVENT_POLICY_REJECTED, pid,
            NEXUS_SECURITY_REASON_INVALID_CAPABILITIES, capabilities, 0U);
        return false;
    }

    lock_security();
    if (active_pid != 0U) {
        const uint32_t current = active_pid;
        unlock_security();
        nexus_security_audit(NEXUS_SECURITY_EVENT_POLICY_REJECTED, pid,
            NEXUS_SECURITY_REASON_CONTEXT_BUSY, current, capabilities);
        return false;
    }
    active_pid = pid;
    active_capabilities = capabilities;
    unlock_security();

    nexus_security_audit(NEXUS_SECURITY_EVENT_CONTEXT_ENTER, pid, 0U,
                         capabilities, 0U);
    return true;
}

void nexus_security_context_leave(uint32_t pid) {
    lock_security();
    if (pid == 0U || active_pid != pid) {
        const uint32_t current = active_pid;
        unlock_security();
        nexus_security_audit(NEXUS_SECURITY_EVENT_POLICY_REJECTED, pid,
            NEXUS_SECURITY_REASON_CONTEXT_MISMATCH, current, 0U);
        return;
    }
    active_pid = 0U;
    active_capabilities = 0U;
    unlock_security();

    nexus_security_audit(NEXUS_SECURITY_EVENT_CONTEXT_LEAVE, pid, 0U, 0U, 0U);
}

uint32_t nexus_security_current_pid(void) {
    lock_security();
    const uint32_t pid = active_pid;
    unlock_security();
    return pid;
}

uint64_t nexus_security_current_capabilities(void) {
    lock_security();
    const uint64_t capabilities = active_capabilities;
    unlock_security();
    return capabilities;
}

bool nexus_security_require(uint64_t capability) {
    if (capability == 0U || (capability & (capability - 1U)) != 0U ||
        !nexus_security_capabilities_valid(capability)) {
        nexus_security_audit(NEXUS_SECURITY_EVENT_POLICY_REJECTED,
                             nexus_security_current_pid(),
                             NEXUS_SECURITY_REASON_INVALID_CAPABILITIES,
                             capability, 0U);
        return false;
    }

    lock_security();
    const uint32_t pid = active_pid;
    const uint64_t granted = active_capabilities;
    unlock_security();

    if (pid != 0U && (granted & capability) == capability) return true;

    nexus_security_audit(NEXUS_SECURITY_EVENT_CAPABILITY_DENIED, pid,
                         (uint16_t)capability, granted, capability);
    return false;
}

size_t nexus_security_audit_read(uint64_t *cursor,
                                 struct nexus_security_event *events,
                                 size_t capacity) {
    if (cursor == NULL || (capacity != 0U && events == NULL)) return 0U;
    if (capacity == 0U) return 0U;

    lock_security();
    const uint64_t newest = next_sequence - 1U;
    if (newest == 0U) {
        *cursor = 1U;
        unlock_security();
        return 0U;
    }

    const uint64_t oldest = newest >= NEXUS_SECURITY_AUDIT_CAPACITY
        ? newest - NEXUS_SECURITY_AUDIT_CAPACITY + 1U : 1U;
    uint64_t current = *cursor == 0U ? 1U : *cursor;
    if (current < oldest) current = oldest;

    size_t copied = 0U;
    while (current <= newest && copied < capacity) {
        const size_t slot =
            (size_t)((current - 1U) % NEXUS_SECURITY_AUDIT_CAPACITY);
        if (audit_ring[slot].sequence != current) break;
        events[copied++] = audit_ring[slot];
        ++current;
    }
    *cursor = current;
    unlock_security();
    return copied;
}
