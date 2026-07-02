bits 64

global syscall_entry

extern syscall_dispatch
extern syscall_take_user_exit_code
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
;
syscall_entry:
    cli

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

    ; syscall_dispatch의 반환값을 보존한 뒤 SYS_exit 요청 여부를 확인한다.
    ; SYS_write의 반환값이 message length이므로, syscall number stack 값에
    ; 의존하면 디버깅이 어렵다. sys_exit handler가 남긴 explicit flag를 본다.
    push rax

    ; 현재 rsp는 16-byte align에서 8만큼 어긋나 있으므로 C call 전 보정한다.
    sub rsp, 8
    call syscall_take_user_exit_code
    add rsp, 8

    mov r10, rax     ; r10 = exit code or SYSCALL_NO_USER_EXIT
    pop rax          ; rax = original syscall return value

    cmp r10, -1
    jne syscall_exit_to_kernel_shell

    pop r11
    pop rcx

    sysretq

syscall_exit_to_kernel_shell:
    ; 현재 rsp는 ring3 user stack이다.
    ; ring3_enter()가 저장해둔 kernel stack으로 돌아간다.
    mov rax, r10
    mov rsp, [rel ring3_saved_kernel_rsp]

    mov dx, 0x10
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    mov ss, dx

    sti
    ret
