.section .text
.global nexus_enter_user
.type nexus_enter_user, @function
.extern usermode_saved_rsp
.extern usermode_saved_cr3

/*
 * void nexus_enter_user(uint64_t cr3, uint64_t rip, uint64_t rsp)
 * This test uses selectors 0x23 (user code) and 0x1B (user data).
 */
nexus_enter_user:
    movq %rsp, usermode_saved_rsp(%rip)
    movq %cr3, %rax
    movq %rax, usermode_saved_cr3(%rip)
    movq %rdi, %cr3

    pushq $0x1B
    pushq %rdx
    pushq $0x02
    pushq $0x23
    pushq %rsi
    iretq

.global nexus_user_return
.type nexus_user_return, @function
nexus_user_return:
    cli
    movq usermode_saved_cr3(%rip), %rax
    movq %rax, %cr3
    movq usermode_saved_rsp(%rip), %rsp
    ret

.section .note.GNU-stack,"",@progbits
