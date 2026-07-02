bits 64

extern exception_handler
extern irq_handler

%macro ISR_NO_ERROR 1
    global isr%1
isr%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro ISR_ERROR 1
    global isr%1
isr%1:
    push qword %1
    jmp isr_common
%endmacro

%macro IRQ_STUB 2
    global irq%1
irq%1:
    push qword %2
    jmp irq_common
%endmacro

; CPU exceptions 0~31
ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_ERROR    8
ISR_NO_ERROR 9
ISR_ERROR    10
ISR_ERROR    11
ISR_ERROR    12
ISR_ERROR    13
ISR_ERROR    14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_ERROR    17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_NO_ERROR 20
ISR_ERROR    21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_ERROR    29
ISR_ERROR    30
ISR_NO_ERROR 31

; Hardware IRQs
; PIC remap 후 IRQ0~15는 IDT vector 32~47로 들어온다.
IRQ_STUB 0,  32
IRQ_STUB 1,  33
IRQ_STUB 2,  34
IRQ_STUB 3,  35
IRQ_STUB 4,  36
IRQ_STUB 5,  37
IRQ_STUB 6,  38
IRQ_STUB 7,  39
IRQ_STUB 8,  40
IRQ_STUB 9,  41
IRQ_STUB 10, 42
IRQ_STUB 11, 43
IRQ_STUB 12, 44
IRQ_STUB 13, 45
IRQ_STUB 14, 46
IRQ_STUB 15, 47

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; 현재 stack 구조:
    ;
    ; [rsp + 15*8] = vector
    ; [rsp + 16*8] = error code
    ; [rsp + 17*8] = RIP
    ; [rsp + 18*8] = CS
    ; [rsp + 19*8] = RFLAGS

    mov rdi, [rsp + 15*8]        ; vector
    mov rsi, [rsp + 16*8]        ; error_code
    mov rdx, [rsp + 17*8]        ; rip
    mov rcx, [rsp + 18*8]        ; cs
    mov r8,  [rsp + 19*8]        ; rflags

    mov rax, cr2
    mov r9, rax                  ; cr2

    call exception_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16                  ; vector + error_code 제거
    iretq

irq_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; 현재 stack 구조:
    ;
    ; [rsp + 15*8] = vector
    ; [rsp + 16*8] = RIP
    ; [rsp + 17*8] = CS
    ; [rsp + 18*8] = RFLAGS

    mov rdi, [rsp + 15*8]        ; vector
    call irq_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 8                   ; vector 제거
    iretq

    section .note.GNU-stack noalloc noexec nowrite progbits