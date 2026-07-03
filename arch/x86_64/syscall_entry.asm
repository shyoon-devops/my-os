bits 64

global syscall_entry

extern syscall_dispatch
extern task_user_saved_kernel_rsp

section .bss
align 16
syscall_user_rsp:    resq 1
syscall_user_rip:    resq 1
syscall_user_rflags: resq 1
syscall_number:      resq 1
syscall_stack:       resb 16384
syscall_stack_top:

section .text

syscall_entry:
    cli

    mov [rel syscall_user_rsp], rsp
    mov [rel syscall_user_rip], rcx
    mov [rel syscall_user_rflags], r11
    mov [rel syscall_number], rax

    cmp rax, 60
    je syscall_exit_direct

    lea rsp, [rel syscall_stack_top]

    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9

    sub rsp, 8
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
    add rsp, 16

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

;
; SYS_exit:
;   현재 task의 saved_kernel_rsp(ring3_enter가 저장)로 커널 스택을 복구하고
;   ring3_enter가 push해 둔 callee-saved 레지스터를 pop한 뒤
;   ring3_enter의 호출자로 ret한다. 반환값(rax) = exit code.
;
syscall_exit_direct:
    ;
    ; rbx에 exit code를 보존한다.
    ; 아래에서 저장된 커널 프레임의 pop rbx로 원래 값이 복구되므로
    ; 여기서 rbx를 잠시 써도 안전하다.
    ;
    mov rbx, rdi

    mov dx, 0x10
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    mov ss, dx

    ;
    ; C 헬퍼 호출을 위해 syscall 커널 스택으로 전환한다.
    ; (아직 user rsp 상태이므로 이 전환 없이는 C를 부를 수 없다)
    ;
    lea rsp, [rel syscall_stack_top]
    call task_user_saved_kernel_rsp

    test rax, rax
    jz .no_user_context

    mov rsp, rax
    mov rax, rbx

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    sti
    ret

.no_user_context:
    ;
    ; ring3_enter 없이 SYS_exit이 들어온 경우.
    ; 복귀할 커널 컨텍스트가 없으므로 여기서 멈춘다.
    ;
    hlt
    jmp .no_user_context
