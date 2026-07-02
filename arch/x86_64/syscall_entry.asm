bits 64

global syscall_entry

extern syscall_dispatch

section .text

;
; syscall_entry
;
; CPU가 syscall instruction으로 들어올 때 도착할 entry point.
;
; x86_64 syscall 진입 시 일반적으로:
;   rax = syscall number
;   rdi = arg0
;   rsi = arg1
;   rdx = arg2
;   r10 = arg3
;   r8  = arg4
;   r9  = arg5
;
;   rcx = user return RIP
;   r11 = user RFLAGS
;
; 주의:
;   syscall은 자동으로 kernel stack으로 바꿔주지 않는다.
;   그래서 이 stub은 Phase 10 전에는 실제 user mode에서 호출하지 않는다.
;
; 지금 Phase 9-B의 목적:
;   - LSTAR에 걸 수 있는 entry symbol을 만든다.
;   - syscall_dispatch() 호출 ABI를 준비한다.
;
syscall_entry:
    cli

    ;
    ; sysretq에 필요하므로 user RIP/RFLAGS를 보존한다.
    ;
    push rcx
    push r11

    ;
    ; syscall_dispatch(number, arg0, arg1, arg2, arg3, arg4, arg5)
    ;
    ; System V AMD64 C ABI:
    ;   rdi, rsi, rdx, rcx, r8, r9, stack...
    ;
    ; syscall ABI:
    ;   rax, rdi, rsi, rdx, r10, r8, r9
    ;
    push r9          ; arg5: 7번째 C 인자, stack 전달

    mov r9, r8       ; arg4
    mov r8, r10      ; arg3
    mov rcx, rdx     ; arg2
    mov rdx, rsi     ; arg1
    mov rsi, rdi     ; arg0
    mov rdi, rax     ; syscall number

    call syscall_dispatch

    add rsp, 8       ; pushed arg5 제거

    pop r11
    pop rcx

    ;
    ; Phase 10에서 user GDT selector, user RIP, user RFLAGS가 준비되면
    ; 여기서 ring3로 복귀한다.
    ;
    sysretq
