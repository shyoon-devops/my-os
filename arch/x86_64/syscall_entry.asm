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

;
; syscall_entry
;
; x86_64 syscall ABI:
;   rax = syscall number
;   rdi = arg0
;   rsi = arg1
;   rdx = arg2
;   r10 = arg3
;   r8  = arg4
;   r9  = arg5
;
; CPU on syscall:
;   rcx = user return RIP
;   r11 = user RFLAGS
;   rsp = user RSP
;
syscall_entry:
    cli

    ; SYSCALL은 x86_64에서 RSP를 자동으로 kernel stack으로 바꾸지 않는다.
    ; 따라서 진입 직후 user return state를 저장하고, ring3_enter()가 저장한
    ; kernel stack으로 전환한 뒤 C syscall handler를 실행한다.
    mov [rel syscall_user_rsp], rsp
    mov [rel syscall_user_rip], rcx
    mov [rel syscall_user_rflags], r11
    mov [rel syscall_number], rax

    mov rsp, [rel ring3_saved_kernel_rsp]

    ; User register 보존.
    ; userland inline syscall wrapper는 rcx/r11/rax 외 레지스터가 보존된다고 본다.
    ; C syscall_dispatch()는 caller-saved register를 자유롭게 덮을 수 있으므로,
    ; userland로 돌아가기 전에 원래 값을 복원한다.
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9

    ; syscall_dispatch(number, arg0, arg1, arg2, arg3, arg4, arg5)
    ; C ABI:
    ;   rdi, rsi, rdx, rcx, r8, r9, stack
    ;
    ; ring3_saved_kernel_rsp는 8-byte misaligned 상태다.
    ; 위에서 register 6개를 저장해도 여전히 8-byte misaligned이고,
    ; arg5 push 뒤 call alignment가 맞는다.
    push r9

    mov r9, r8
    mov r8, r10
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax

    ; SYS_read 같은 blocking syscall은 keyboard IRQ가 들어와야 깨어난다.
    ; C syscall handler 실행 중에는 IRQ를 켠다.
    sti
    call syscall_dispatch
    cli

    add rsp, 8

    cmp qword [rel syscall_number], 60
    je syscall_exit_to_kernel_shell

    ; dispatch return value는 rax에 둔 채 user register를 복원한다.
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi

    ; 일반 syscall은 저장해 둔 user state로 iretq 복귀한다.
    mov rdx, [rel syscall_user_rsp]
    mov r11, [rel syscall_user_rflags]
    mov rcx, [rel syscall_user_rip]

    push qword 0x23
    push rdx
    push r11
    push qword 0x2B
    push rcx
    iretq

syscall_exit_to_kernel_shell:
    ; SYS_exit은 userland로 돌아가지 않으므로 저장해 둔 user register를 폐기한다.
    add rsp, 48

    ; rax에는 syscall_dispatch(SYS_exit)의 반환값, 즉 exit code가 있다.
    ; ring3_enter()의 원래 kernel stack으로 돌아가 ret한다.
    mov rsp, [rel ring3_saved_kernel_rsp]

    mov dx, 0x10
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    mov ss, dx

    sti
    ret
