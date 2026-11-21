.section .text
.global arch_context_switch
.type arch_context_switch, @function

/*
 * void arch_context_switch(uintptr_t *old_stack, uintptr_t new_stack)
 *
 * Save the callee-saved integer registers and the current RSP. Restore the
 * next stack frame and return into the saved caller (or a new thread bootstrap).
 * Interrupts remain disabled during this first cooperative scheduler test.
 */
arch_context_switch:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, (%rdi)
    movq %rsi, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

.section .note.GNU-stack,"",@progbits
