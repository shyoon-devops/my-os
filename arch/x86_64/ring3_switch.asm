bits 64

global ring3_enter
global ring3_user_entry
global ring3_user_blob_start
global ring3_user_blob_end

section .text

;
; u64 ring3_enter(u64 user_rip, u64 user_rsp, u64* saved_kernel_rsp_slot);
;
; rdi = user RIP
; rsi = user RSP
; rdx = 커널 rsp를 저장할 슬롯 (현재 task의 user_context.saved_kernel_rsp)
;
; 이 함수는 iretq로 ring3에 진입한다.
; ring3에서 SYS_exit을 호출하면 syscall_entry.asm이
; 현재 task의 saved_kernel_rsp로 커널 스택을 복구하고,
; 아래에서 push한 callee-saved 레지스터를 pop한 뒤 ret로 돌아온다.
;
; callee-saved 레지스터를 push해 두는 이유:
; user 코드가 rbx/rbp/r12~r15를 마음대로 쓰기 때문에,
; 보존하지 않으면 SYS_exit 복귀 후 C 호출자의 레지스터가 깨진다.
;
ring3_enter:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov [rdx], rsp

    ;
    ; ring3 data selector
    ;
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;
    ; iretq frame:
    ;   SS
    ;   RSP
    ;   RFLAGS
    ;   CS
    ;   RIP
    ;
    push qword 0x23
    push rsi

    pushfq
    or qword [rsp], 0x200

    push qword 0x2B
    push rdi

    iretq

;
; ring3 test payload.
;
; 실제 user ELF가 아니라 ring3 진입 자체를 검증하기 위한
; 최소 user-mode 코드다.
;
ring3_user_blob_start:

ring3_user_entry:
    ;
    ; SYS_write(1, message, len)
    ;
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel ring3_message]
    mov rdx, ring3_message_len
    syscall

    ;
    ; SYS_exit(42)
    ;
    mov rax, 60
    mov rdi, 42
    syscall

.hang:
    pause
    jmp .hang

ring3_message:
    db "hello from ring3 via syscall", 10
ring3_message_len equ $ - ring3_message

ring3_user_blob_end:
