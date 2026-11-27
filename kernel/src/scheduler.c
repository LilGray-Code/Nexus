#include "scheduler.h"

#include "timer.h"
#include "trace.h"

#include <stddef.h>
#include <stdint.h>

#define NEXUS_MAX_THREADS NEXUS_SCHEDULER_MAX_THREADS
#define NEXUS_THREAD_STACK_SIZE 16384U
#define RFLAGS_INTERRUPT_ENABLE (1ULL << 9)

enum nexus_thread_state {
    NEXUS_THREAD_UNUSED = 0,
    NEXUS_THREAD_RUNNABLE,
    NEXUS_THREAD_SLEEPING,
    NEXUS_THREAD_FINISHED
};

struct nexus_thread {
    uintptr_t saved_stack;
    nexus_thread_entry_t entry;
    void *argument;
    /* Updated by timer IRQs; force foreground code to reload these fields. */
    volatile enum nexus_thread_state state;
    volatile uint64_t wake_tick;
    bool saved_interrupts_enabled;
    uint8_t stack[NEXUS_THREAD_STACK_SIZE] __attribute__((aligned(16)));
};

static struct nexus_thread main_thread;
static struct nexus_thread threads[NEXUS_MAX_THREADS];
static size_t thread_count;
static struct nexus_thread *current_thread;
static bool scheduler_started;
/* IRQ-context preemption remains opt-in outside scheduler-safe execution windows. */
static bool preemption_enabled;

extern void arch_context_switch(uintptr_t *old_stack, uintptr_t new_stack);

static uint64_t interrupt_flags(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    return flags;
}

static void restore_interrupt_state(bool enabled) {
    if (enabled) {
        __asm__ volatile ("sti" : : : "memory");
    } else {
        __asm__ volatile ("cli" : : : "memory");
    }
}

__attribute__((noreturn))
static void scheduler_thread_exit(void);

__attribute__((noreturn))
static void thread_bootstrap(void) {
    struct nexus_thread *self = current_thread;
    if (self == NULL || self == &main_thread || self->entry == NULL) {
        for (;;) __asm__ volatile ("cli; hlt" ::: "memory");
    }

    /* New kernel threads begin with interrupts enabled. */
    __asm__ volatile ("sti" : : : "memory");
    self->entry(self->argument);
    scheduler_thread_exit();
}

void scheduler_init(void) {
    __asm__ volatile ("cli" : : : "memory");
    main_thread.saved_stack = 0U;
    main_thread.entry = NULL;
    main_thread.argument = NULL;
    main_thread.state = NEXUS_THREAD_RUNNABLE;
    main_thread.wake_tick = 0U;
    main_thread.saved_interrupts_enabled = false;

    for (size_t i = 0; i < NEXUS_MAX_THREADS; ++i) {
        threads[i].saved_stack = 0U;
        threads[i].entry = NULL;
        threads[i].argument = NULL;
        threads[i].state = NEXUS_THREAD_UNUSED;
        threads[i].wake_tick = 0U;
        threads[i].saved_interrupts_enabled = false;
    }

    thread_count = 0U;
    current_thread = &main_thread;
    scheduler_started = false;
    preemption_enabled = false;
}

bool scheduler_create_thread(nexus_thread_entry_t entry, void *argument) {
    if (entry == NULL) return false;

    const bool restore_if = (interrupt_flags() & RFLAGS_INTERRUPT_ENABLE) != 0U;
    __asm__ volatile ("cli" : : : "memory");
    size_t slot = NEXUS_MAX_THREADS;
    for (size_t i = 0U; i < thread_count; ++i) {
        if (threads[i].state == NEXUS_THREAD_FINISHED ||
            threads[i].state == NEXUS_THREAD_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot == NEXUS_MAX_THREADS) {
        if (thread_count >= NEXUS_MAX_THREADS) {
            restore_interrupt_state(restore_if);
            return false;
        }
        slot = thread_count;
        ++thread_count;
    }

    struct nexus_thread *thread = &threads[slot];
    thread->saved_stack = 0U;
    thread->entry = entry;
    thread->argument = argument;
    thread->state = NEXUS_THREAD_RUNNABLE;
    thread->wake_tick = 0U;
    thread->saved_interrupts_enabled = true;

    uintptr_t stack_top =
        (uintptr_t)(thread->stack + sizeof(thread->stack));
    stack_top &= ~(uintptr_t)0xFULL;

    /*
     * arch_context_switch restores six callee-saved registers and executes
     * ret. The synthetic frame gives the new C entry SysV RSP % 16 == 8.
     */
    uintptr_t *initial_stack =
        (uintptr_t *)(stack_top - (8U * sizeof(uintptr_t)));
    for (size_t i = 0; i < 6U; ++i) initial_stack[i] = 0U;
    initial_stack[6] = (uintptr_t)thread_bootstrap;
    initial_stack[7] = 0U;
    thread->saved_stack = (uintptr_t)initial_stack;

    restore_interrupt_state(restore_if);
    return true;
}

static struct nexus_thread *next_runnable_thread(
    const struct nexus_thread *after) {
    const size_t total_slots = thread_count + 1U;
    size_t current_index = 0U; /* The main thread occupies slot zero. */

    if (after != &main_thread) {
        for (size_t i = 0U; i < thread_count; ++i) {
            if (&threads[i] == after) {
                current_index = i + 1U;
                break;
            }
        }
    }

    for (size_t step = 1U; step <= total_slots; ++step) {
        const size_t index = (current_index + step) % total_slots;
        if (index == 0U) {
            if (main_thread.state == NEXUS_THREAD_RUNNABLE)
                return &main_thread;
            continue;
        }
        struct nexus_thread *candidate = &threads[index - 1U];
        if (candidate->state == NEXUS_THREAD_RUNNABLE) return candidate;
    }
    return NULL;
}

void scheduler_start(void) {
    if (scheduler_started || thread_count == 0U) return;
    __asm__ volatile ("cli" : : : "memory");
    scheduler_started = true;
    current_thread = &main_thread;

    struct nexus_thread *next = next_runnable_thread(&main_thread);
    if (next != &main_thread) {
        current_thread = next;
        arch_context_switch(&main_thread.saved_stack, next->saved_stack);
    }

    /* A timer preemption can return here from an interrupt-gated context. */
    current_thread = &main_thread;
    __asm__ volatile ("sti" : : : "memory");
}

static uint64_t scheduler_trace_thread_id(const struct nexus_thread *thread) {
    if (thread == &main_thread) return 0U;
    for (size_t i = 0U; i < thread_count; ++i)
        if (thread == &threads[i]) return (uint64_t)i + 1U;
    return UINT64_MAX;
}

void scheduler_yield(void) {
    /*
     * Preserve the caller's interrupt state, then take the scheduler lock
     * before reading current_thread or selecting a destination. The previous
     * ordering allowed a timer IRQ between selection and CLI to invalidate
     * the saved current/next pair.
     */
    const bool restore_if =
        (interrupt_flags() & RFLAGS_INTERRUPT_ENABLE) != 0U;
    __asm__ volatile ("cli" : : : "memory");

    struct nexus_thread *old = current_thread;
    if (old == NULL || !scheduler_started) {
        restore_interrupt_state(restore_if);
        return;
    }

    struct nexus_thread *next = next_runnable_thread(old);
    if (next == NULL || next == old) {
        restore_interrupt_state(restore_if);
        return;
    }

    nexus_trace_emit(NEXUS_TRACE_EVENT_SCHEDULER_SWITCH, timer_ticks(),
                     scheduler_trace_thread_id(old),
                     scheduler_trace_thread_id(next));
    old->saved_interrupts_enabled = restore_if;
    current_thread = next;
    arch_context_switch(&old->saved_stack, next->saved_stack);

    /* Restore the IF state of the suspended call site, not the new thread's. */
    restore_interrupt_state(old->saved_interrupts_enabled);
}

/*
 * Called from the timer interrupt after the global tick is advanced. Only
 * sleeping threads are inspected here; context switching remains in the
 * existing IRQ-exit preemption point or an explicit scheduler safe point.
 */
void scheduler_timer_tick(uint64_t now) {
    if (main_thread.state == NEXUS_THREAD_SLEEPING &&
        now >= main_thread.wake_tick) {
        main_thread.state = NEXUS_THREAD_RUNNABLE;
    }

    for (size_t i = 0U; i < thread_count; ++i) {
        struct nexus_thread *thread = &threads[i];
        if (thread->state == NEXUS_THREAD_SLEEPING &&
            now >= thread->wake_tick) {
            thread->state = NEXUS_THREAD_RUNNABLE;
        }
    }
}

/*
 * Put the current scheduler thread to sleep until a timer deadline.
 * When no thread is runnable, the blocked caller idles with STI;HLT on its
 * own stack. Timer IRQs wake expired threads; no separate idle stack is
 * required for this single-BSP scheduler stage.
 */
bool scheduler_sleep_ticks(uint64_t ticks) {
    if (ticks == 0U) return true;

    const bool restore_if =
        (interrupt_flags() & RFLAGS_INTERRUPT_ENABLE) != 0U;
    __asm__ volatile ("cli" : : : "memory");

    struct nexus_thread *old = current_thread;
    const uint64_t now = timer_ticks();
    if (!scheduler_started || old == NULL ||
        old->state != NEXUS_THREAD_RUNNABLE ||
        UINT64_MAX - now < ticks) {
        restore_interrupt_state(restore_if);
        return false;
    }

    old->wake_tick = now + ticks;
    old->state = NEXUS_THREAD_SLEEPING;
    while (old->state == NEXUS_THREAD_SLEEPING) {
        scheduler_yield();
        if (old->state == NEXUS_THREAD_SLEEPING) {
            /*
             * STI immediately followed by HLT is atomic with respect to a
             * pending maskable IRQ, so a timer cannot be lost between the
             * deadline check and entering the idle wait.
             */
            __asm__ volatile ("sti; hlt; cli" : : : "memory");
        }
    }

    restore_interrupt_state(restore_if);
    return old->state == NEXUS_THREAD_RUNNABLE;
}

void scheduler_set_preemption_enabled(bool enabled) {
    const bool restore_if =
        (interrupt_flags() & RFLAGS_INTERRUPT_ENABLE) != 0U;
    __asm__ volatile ("cli" : : : "memory");
    preemption_enabled = enabled;
    restore_interrupt_state(restore_if);
}

void scheduler_preempt(void) {
    if (!preemption_enabled || !scheduler_started || current_thread == NULL)
        return;
    scheduler_yield();
}

void scheduler_wait(void) {
    if (!scheduler_started) return;
    for (;;) {
        bool runnable = false;
        for (size_t i = 0U; i < thread_count; ++i) {
            if (threads[i].state == NEXUS_THREAD_RUNNABLE ||
                threads[i].state == NEXUS_THREAD_SLEEPING) {
                runnable = true;
                break;
            }
        }
        if (!runnable) break;
        __asm__ volatile ("sti; hlt" : : : "memory");
        /* Timer IRQs wake expired contexts; this is the main-context safe point. */
        scheduler_yield();
    }
    __asm__ volatile ("cli" : : : "memory");
}

__attribute__((noreturn))
static void scheduler_thread_exit(void) {
    struct nexus_thread *old = current_thread;
    __asm__ volatile ("cli" : : : "memory");
    old->state = NEXUS_THREAD_FINISHED;

    /*
     * A final worker can exit while every other worker and the BSP main
     * context is sleeping. Keep the finished stack as a temporary idle
     * context until a timer makes some other context runnable.
     */
    for (;;) {
        struct nexus_thread *next = next_runnable_thread(old);
        if (next != NULL) {
            current_thread = next;
            arch_context_switch(&old->saved_stack, next->saved_stack);
        }
        __asm__ volatile ("sti; hlt; cli" : : : "memory");
    }
}
