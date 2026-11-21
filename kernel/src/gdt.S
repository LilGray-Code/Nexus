.section .text
.global nexus_gdt_load
.type nexus_gdt_load, @function

/*
 * void nexus_gdt_load(const struct gdt_pointer *pointer,
 *                     uint16_t tss_selector)
 * RDI = GDTR pointer, SI = TSS selector.
 */
nexus_gdt_load:
    lgdt (%rdi)

    /* Reload CS using a far return, then reload the kernel data selectors. */
    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    movw %si, %ax
    ltr %ax
    ret

.section .note.GNU-stack,"",@progbits
