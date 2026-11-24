.section .text.start,"ax"
.global _start
.type _start, @function
.extern main

/* The loader supplies a page-aligned user stack. Calling main creates the
 * SysV x86-64 entry alignment expected by compiled C functions.
 */
_start:
    call main
    movq %rax, %rdi
    movq $1, %rax
    int $0x80
1:
    pause
    jmp 1b

.section .note.GNU-stack,"",@progbits
