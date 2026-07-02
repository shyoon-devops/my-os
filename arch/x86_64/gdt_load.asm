bits 64

global gdt_load
global tss_load

section .text

;
; void gdt_load(void* gdtr);
;
; rdi = &gdt_descriptor
;
gdt_load:
    lgdt [rdi]

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ;
    ; CS는 mov로 바꿀 수 없으므로 far return으로 0x08 selector를 reload한다.
    ;
    push qword 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq

.reload_cs:
    ret

;
; void tss_load(u16 selector);
;
; rdi = selector
;
tss_load:
    ltr di
    ret
