bits 64

global syscall_entry

extern syscall_dispatch
extern ring3_saved_kernel_rsp

section .bss
align 8
syscall_user_rsp:    resq 1
syscall_user_rip:    resq 1
syscall_user_rflags: resq 1
syscall_number:      resq 1

section .text

syscall_entry:
    cli

    mov [rel syscall_user_rsp], rsp
    mov [rel syscall_user_rip], rcx
    mov [rel syscall_user_rflags], r11
    mov [rel syscall_number], rax

    cmp rax, 60
    je syscall_exit_direct

    mov rsp, [rel ring3_saved_kernel_rsp]

    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9

    push r9

    mov r9, r8
    mov r8, r10
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax

    cmp qword [rel syscall_number], 0
    je syscall_call_dispatch_irq_on

syscall_call_dispatch_irq_off:
    call syscall_dispatch
    jmp syscall_after_dispatch

syscall_call_dispatch_irq_on:
    sti
    call syscall_dispatch
    cli

syscall_after_dispatch:
    add rsp, 8

    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi

    push qword 0x23
    push qword [rel syscall_user_rsp]
    push qword [rel syscall_user_rflags]
    push qword 0x2B
    push qword [rel syscall_user_rip]
    iretq

syscall_exit_direct:
    mov rax, rdi
    mov rsp, [rel ring3_saved_kernel_rsp]

    mov dx, 0x10
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    mov ss, dx

    sti
    ret
