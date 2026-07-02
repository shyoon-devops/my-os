bits 64

global syscall_entry

extern syscall_dispatch
extern ring3_saved_kernel_rsp

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

    ; syscall 진입 직후 rcx/r11에 들어 있는 user return state를 보존한다.
    push rcx
    push r11

    ; Phase 10-C/10-E에서는 SYS_exit만 kernel shell 복귀 신호다.
    ; 일반 syscall은 dispatch 후 iretq로 userland에 복귀한다.
    cmp rax, 60
    je syscall_dispatch_exit

syscall_dispatch_return_user:
    ; syscall_dispatch(number, arg0, arg1, arg2, arg3, arg4, arg5)
    ; C ABI:
    ;   rdi, rsi, rdx, rcx, r8, r9, stack
    ;
    ; rcx/r11 push 이후 rsp는 16-byte aligned 상태다.
    ; 7번째 인자 push 전에 padding 8 byte를 넣어 call alignment를 맞춘다.
    sub rsp, 8
    push r9

    mov r9, r8
    mov r8, r10
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax

    ; SYS_read 같은 blocking syscall은 keyboard IRQ가 들어와야 깨어난다.
    ; syscall_entry가 cli 상태로 dispatch하면 wait queue가 IRQ disabled 상태로
    ; 잠들 수 있으므로, C syscall handler 실행 중에는 IRQ를 다시 켠다.
    sti
    call syscall_dispatch
    cli

    add rsp, 16

    ; saved user state 복원.
    pop r11
    pop rcx

    ; SYSRET 대신 IRETQ를 사용한다.
    ; 현재 toy kernel은 ring3 진입도 iretq 기반이고, 이 경로가 selector/STAR
    ; 의존성을 제거해서 Phase 10-E 디버깅 범위를 줄인다.
    mov rdx, rsp
    push qword 0x23
    push rdx
    push r11
    push qword 0x2B
    push rcx
    iretq

syscall_dispatch_exit:
    sub rsp, 8
    push r9

    mov r9, r8
    mov r8, r10
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax

    sti
    call syscall_dispatch
    cli

    add rsp, 16

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
