#include "scheduler.h"

#include <stddef.h>
#include <stdint.h>

#define NEXUS_MAX_THREADS NEXUS_SCHEDULER_MAX_THREADS
#define NEXUS_THREAD_STACK_SIZE 16384U
#define RFLAGS_INTERRUPT_ENABLE (1ULL << 9)

struct nexus_thread {
    uintptr_t saved_stack;
    nexus_thread_entry_t entry;
    void *argument;
    bool finished;
    bool saved_interrupts_enabled;
    uint8_t stack[NEXUS_THREAD_STACK_SIZE] __attribute__((aligned(16)));
};

static struct nexus_thread main_thread;
static struct nexus_thread threads[NEXUS_MAX_THREADS];
static size_t thread_count;
static struct nexus_thread *current_thread;
static bool scheduler_started;
/* IRQ-context preemption stays opt-in until its dedicated QEMU test passes. */
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
    main_thread.finished = false;
    main_thread.saved_interrupts_enabled = false;

    for (size_t i = 0; i < NEXUS_MAX_THREADS; ++i) {
        threads[i].saved_stack = 0U;
        threads[i].entry = NULL;
        threads[i].argument = NULL;
        threads[i].finished = true;
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
        if (threads[i].finished) {
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
    thread->finished = false;
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
        if (index == 0U) return &main_thread;
        struct nexus_thread *candidate = &threads[index - 1U];
        if (!candidate->finished) return candidate;
    }
    return &main_thread;
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
    if (next == old) {
        restore_interrupt_state(restore_if);
        return;
    }

    old->saved_interrupts_enabled = restore_if;
    current_thread = next;
    arch_context_switch(&old->saved_stack, next->saved_stack);

    /* Restore the IF state of the suspended call site, not the new thread's. */
    restore_interrupt_state(old->saved_interrupts_enabled);
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
            if (!threads[i].finished) {
                runnable = true;
                break;
            }
        }
        if (!runnable) break;
        __asm__ volatile ("sti; hlt" : : : "memory");
        /* Timer IRQs wake the main thread; thread switches happen at this
         * explicit safe point until IRQ-stack preemption is independently
         * validated on both supported firmware paths. */
        scheduler_yield();
    }
    __asm__ volatile ("cli" : : : "memory");
}

__attribute__((noreturn))
static void scheduler_thread_exit(void) {
    struct nexus_thread *old = current_thread;
    __asm__ volatile ("cli" : : : "memory");
    old->finished = true;

    struct nexus_thread *next = next_runnable_thread(old);
    current_thread = next;
    arch_context_switch(&old->saved_stack, next->saved_stack);

    /* Finished thread stacks are never scheduled again. */
    for (;;) __asm__ volatile ("cli; hlt" ::: "memory");
}
