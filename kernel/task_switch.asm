bits 64

global task_context_switch
global task_entry_trampoline

extern task_trampoline

;
; void task_context_switch(u64* old_rsp, u64 new_rsp)
;
; rdi = &old_rsp
; rsi = new_rsp
;
task_context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp
    mov rsp, rsi

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret


;
; 새 task가 처음 실행될 때 ret로 여기 들어온다.
; C 함수 task_trampoline()은 정상 call 규약으로 호출한다.
;
task_entry_trampoline:
    call task_trampoline

.hang:
    hlt
    jmp .hang


section .note.GNU-stack noalloc noexec nowrite progbits
