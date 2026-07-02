bits 64

global syscall_entry

extern syscall_dispatch
extern ring3_saved_kernel_rsp

section .bss
align 8
syscall_entry_number: resq 1

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
;
syscall_entry:
    cli

    mov [rel syscall_entry_number], rax

    ; sysretq에 필요한 값.
    push rcx
    push r11

    ; syscall_dispatch(number, arg0, arg1, arg2, arg3, arg4, arg5)
    ; C ABI:
    ;   rdi, rsi, rdx, rcx, r8, r9, stack
    push r9          ; arg5, 7번째 C 인자

    mov r9, r8       ; arg4
    mov r8, r10      ; arg3
    mov rcx, rdx     ; arg2
    mov rdx, rsi     ; arg1
    mov rsi, rdi     ; arg0
    mov rdi, rax     ; syscall number

    call syscall_dispatch

    add rsp, 8       ; arg5 제거

    ; Phase 10-C/10-E에서는 SYS_exit만 kernel shell 복귀 신호다.
    ; SYS_write 같은 일반 syscall은 sysretq로 userland에 복귀해야 한다.
    cmp qword [rel syscall_entry_number], 60
    je syscall_exit_to_kernel_shell

    pop r11
    pop rcx

    sysretq

syscall_exit_to_kernel_shell:
    ; 현재 rsp는 ring3 user stack이다.
    ; ring3_enter()가 저장해둔 kernel stack으로 돌아간다.
    ; rax에는 syscall_dispatch(SYS_exit)의 반환값, 즉 exit code가 있다.
    mov rsp, [rel ring3_saved_kernel_rsp]

    mov dx, 0x10
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    mov ss, dx

    sti
    ret
