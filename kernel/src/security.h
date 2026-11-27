#ifndef NEXUS_SECURITY_H
#define NEXUS_SECURITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Kernel-issued process capabilities. Applications cannot modify this mask:
 * it is selected by trusted kernel launch code and bound to the active process.
 */
#define NEXUS_CAP_CONSOLE_WRITE (1ULL << 0)
#define NEXUS_CAP_FILE_READ     (1ULL << 1)
#define NEXUS_CAP_FILE_WRITE    (1ULL << 2)
#define NEXUS_CAP_INPUT         (1ULL << 3)
#define NEXUS_CAP_GAMEPAD       (1ULL << 4)
#define NEXUS_CAP_DISPLAY       (1ULL << 5)
#define NEXUS_CAP_AUDIO         (1ULL << 6)
#define NEXUS_CAP_CLOCK         (1ULL << 7)
#define NEXUS_CAP_SYSTEM_INFO   (1ULL << 8)
#define NEXUS_CAP_NETWORK       (1ULL << 9)
#define NEXUS_CAP_DRIVER_MANAGE (1ULL << 10)
#define NEXUS_CAP_ADMIN         (1ULL << 11)

#define NEXUS_CAP_ALL ((1ULL << 12) - 1ULL)

/* Ordinary native applications get only the services used by desktop apps. */
#define NEXUS_CAP_APP_DEFAULT \
    (NEXUS_CAP_CONSOLE_WRITE | NEXUS_CAP_FILE_READ | NEXUS_CAP_INPUT | \
     NEXUS_CAP_DISPLAY | NEXUS_CAP_CLOCK)

/* Signed packages without a capability manifest receive a deliberately small profile. */
#define NEXUS_CAP_PACKAGE_DEFAULT \
    (NEXUS_CAP_CONSOLE_WRITE | NEXUS_CAP_CLOCK)

#define NEXUS_SECURITY_AUDIT_CAPACITY 64U

enum nexus_security_event_type {
    NEXUS_SECURITY_EVENT_CONTEXT_ENTER = 1,
    NEXUS_SECURITY_EVENT_CONTEXT_LEAVE,
    NEXUS_SECURITY_EVENT_CAPABILITY_DENIED,
    NEXUS_SECURITY_EVENT_POLICY_REJECTED,
    NEXUS_SECURITY_EVENT_USER_EXCEPTION,
    NEXUS_SECURITY_EVENT_PACKAGE_REJECTED,
    NEXUS_SECURITY_EVENT_DRIVER_REJECTED,
    NEXUS_SECURITY_EVENT_UPDATE_REJECTED,
    NEXUS_SECURITY_EVENT_AUTH_FAILURE,
    NEXUS_SECURITY_EVENT_RECOVERY
};

enum nexus_security_reason {
    NEXUS_SECURITY_REASON_INVALID_CAPABILITIES = 1,
    NEXUS_SECURITY_REASON_CONTEXT_BUSY,
    NEXUS_SECURITY_REASON_CONTEXT_MISMATCH,
    NEXUS_SECURITY_REASON_NX_UNAVAILABLE,
    NEXUS_SECURITY_REASON_PACKAGE_VERIFICATION,
    NEXUS_SECURITY_REASON_DRIVER_UNTRUSTED,
    NEXUS_SECURITY_REASON_UPDATE_VERIFICATION,
    NEXUS_SECURITY_REASON_AUTHENTICATION
};

struct nexus_security_event {
    uint64_t sequence;
    uint64_t detail;
    uint64_t auxiliary;
    uint32_t actor_pid;
    uint16_t type;
    uint16_t code;
};

/* Initialize before starting secondary CPUs, scheduler workers, or user mode. */
void nexus_security_init(void);
bool nexus_security_capabilities_valid(uint64_t capabilities);
bool nexus_security_context_enter(uint32_t pid, uint64_t capabilities);
void nexus_security_context_leave(uint32_t pid);
uint32_t nexus_security_current_pid(void);
uint64_t nexus_security_current_capabilities(void);
/* Only a single known capability bit may be queried at a time. */
bool nexus_security_require(uint64_t capability);
void nexus_security_audit(uint16_t type, uint32_t actor_pid, uint16_t code,
                          uint64_t detail, uint64_t auxiliary);
/*
 * Read events from a monotonically increasing cursor. Set *cursor to 1 for
 * the oldest retained event. If records were overwritten, the cursor advances
 * to the oldest event still present. No user-mode API exposes this yet.
 */
size_t nexus_security_audit_read(uint64_t *cursor,
                                struct nexus_security_event *events,
                                size_t capacity);

#endif
