#include "scheduler.h"

#include <stddef.h>
#include <stdint.h>

#define NEXUS_MAX_THREADS 4U
#define NEXUS_THREAD_STACK_SIZE 16384U

struct nexus_thread {
    uintptr_t saved_stack;
    nexus_thread_entry_t entry;
    void *argument;
    bool finished;
    uint8_t stack[NEXUS_THREAD_STACK_SIZE] __attribute__((aligned(16)));
};

static struct nexus_thread main_thread;
static struct nexus_thread threads[NEXUS_MAX_THREADS];
static size_t thread_count;
static struct nexus_thread *current_thread;
static bool scheduler_started;

extern void arch_context_switch(uintptr_t *old_stack, uintptr_t new_stack);

__attribute__((noreturn))
static void scheduler_thread_exit(void);

__attribute__((noreturn))
static void thread_bootstrap(void) {
    struct nexus_thread *self = current_thread;

    if (self == NULL || self == &main_thread || self->entry == NULL) {
        for (;;) __asm__ volatile ("cli; hlt" ::: "memory");
    }

    self->entry(self->argument);
    scheduler_thread_exit();
}

void scheduler_init(void) {
    main_thread.saved_stack = 0;
    main_thread.entry = NULL;
    main_thread.argument = NULL;
    main_thread.finished = false;

    for (size_t i = 0; i < NEXUS_MAX_THREADS; ++i) {
        threads[i].saved_stack = 0;
        threads[i].entry = NULL;
        threads[i].argument = NULL;
        threads[i].finished = true;
    }

    thread_count = 0;
    current_thread = &main_thread;
    scheduler_started = false;
}

bool scheduler_create_thread(nexus_thread_entry_t entry, void *argument) {
    if (entry == NULL || scheduler_started ||
        thread_count >= NEXUS_MAX_THREADS) {
        return false;
    }

    struct nexus_thread *thread = &threads[thread_count];
    thread->entry = entry;
    thread->argument = argument;
    thread->finished = false;

    uintptr_t stack_top =
        (uintptr_t)(thread->stack + sizeof(thread->stack));
    stack_top &= ~(uintptr_t)0xFULL;

    /*
     * arch_context_switch restores six callee-saved registers and executes
     * ret. Place a synthetic restore frame so the new C entry begins with
     * the SysV-required RSP alignment (RSP % 16 == 8).
     */
    uintptr_t *initial_stack =
        (uintptr_t *)(stack_top - (8U * sizeof(uintptr_t)));
    for (size_t i = 0; i < 6; ++i) initial_stack[i] = 0;
    initial_stack[6] = (uintptr_t)thread_bootstrap;
    initial_stack[7] = 0;
    thread->saved_stack = (uintptr_t)initial_stack;

    ++thread_count;
    return true;
}

static struct nexus_thread *next_runnable_thread(
    const struct nexus_thread *after) {
    if (thread_count == 0) return &main_thread;

    size_t start = 0;
    if (after != &main_thread) {
        for (size_t i = 0; i < thread_count; ++i) {
            if (&threads[i] == after) {
                start = (i + 1) % thread_count;
                break;
            }
        }
    }

    for (size_t offset = 0; offset < thread_count; ++offset) {
        size_t index = (start + offset) % thread_count;
        if (!threads[index].finished) return &threads[index];
    }

    return &main_thread;
}

void scheduler_start(void) {
    if (scheduler_started || thread_count == 0) return;
    scheduler_started = true;
    current_thread = &main_thread;

    struct nexus_thread *next = next_runnable_thread(&main_thread);
    if (next == &main_thread) return;

    current_thread = next;
    arch_context_switch(&main_thread.saved_stack, next->saved_stack);

    /* All worker threads have returned control to the boot thread. */
    current_thread = &main_thread;
}

void scheduler_yield(void) {
    struct nexus_thread *old = current_thread;
    if (old == NULL || old == &main_thread) return;

    struct nexus_thread *next = next_runnable_thread(old);
    if (next == old) return;

    current_thread = next;
    arch_context_switch(&old->saved_stack, next->saved_stack);
}

__attribute__((noreturn))
static void scheduler_thread_exit(void) {
    struct nexus_thread *old = current_thread;
    old->finished = true;

    struct nexus_thread *next = next_runnable_thread(old);
    current_thread = next;
    arch_context_switch(&old->saved_stack, next->saved_stack);

    /* Finished thread stacks are never scheduled again. */
    for (;;) __asm__ volatile ("cli; hlt" ::: "memory");
}
