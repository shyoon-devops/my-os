bits 32

%define MB2_HEADER_MAGIC      0xE85250D6
%define MB2_ARCHITECTURE_I386 0
%define MB2_BOOTLOADER_MAGIC  0x36D76289

%define CR0_PE  0x00000001
%define CR0_PG  0x80000000
%define CR4_PAE 0x00000020

%define EFER_MSR 0xC0000080
%define EFER_LME 0x00000100

CODE_SEG equ gdt64_code - gdt64_start
DATA_SEG equ gdt64_data - gdt64_start

section .multiboot
align 8

mb2_header_start:
    dd MB2_HEADER_MAGIC
    dd MB2_ARCHITECTURE_I386
    dd mb2_header_end - mb2_header_start
    dd -(MB2_HEADER_MAGIC + MB2_ARCHITECTURE_I386 + (mb2_header_end - mb2_header_start))

    ; Multiboot2 end tag
    dw 0
    dw 0
    dd 8

mb2_header_end:


section .text
global _start
extern kernel_main

_start:
    cli

    ; GRUB이 Multiboot2로 진입했는지 확인
    cmp eax, MB2_BOOTLOADER_MAGIC
    jne hang32

    ; GRUB이 넘겨준 값 저장
    mov [mb2_magic], eax
    mov [mb2_info], ebx

    ; 32비트 entry용 임시 stack
    mov esp, stack_top

    ; long mode 진입용 page table 준비
    call setup_page_tables

    ; 64비트용 GDT 등록
    lgdt [gdt64_descriptor]

    ; CR3 = PML4 주소
    mov eax, p4_table
    mov cr3, eax

    ; CR4.PAE = 1
    mov eax, cr4
    or eax, CR4_PAE
    mov cr4, eax

    ; EFER.LME = 1
    mov ecx, EFER_MSR
    rdmsr
    or eax, EFER_LME
    wrmsr

    ; CR0.PE = 1, CR0.PG = 1
    mov eax, cr0
    or eax, CR0_PE | CR0_PG
    mov cr0, eax

    ; far jump로 64비트 code segment 진입
    jmp CODE_SEG:long_mode_start


hang32:
    hlt
    jmp hang32


setup_page_tables:
    ; PML4[0] -> PDPT
    mov eax, p3_table
    or eax, 0x03
    mov dword [p4_table], eax
    mov dword [p4_table + 4], 0

    ; PDPT[0] -> PD
    mov eax, p2_table
    or eax, 0x03
    mov dword [p3_table], eax
    mov dword [p3_table + 4], 0

    ; PD[0..511] = 2MB page 512개
    ; 첫 1GB를 identity map
    xor ecx, ecx

.map_p2:
    mov eax, ecx
    shl eax, 21
    or eax, 0x83
    mov dword [p2_table + ecx * 8], eax
    mov dword [p2_table + ecx * 8 + 4], 0

    inc ecx
    cmp ecx, 512
    jne .map_p2

    ret


section .rodata
align 8

gdt64_start:
gdt64_null:
    dq 0x0000000000000000

gdt64_code:
    dq 0x00209A0000000000

gdt64_data:
    dq 0x0000920000000000

gdt64_end:

gdt64_descriptor:
    dw gdt64_end - gdt64_start - 1
    dd gdt64_start


section .text
bits 64

long_mode_start:
    ; data segment selector 설정
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 64비트 stack 설정
    mov rsp, stack_top
    xor rbp, rbp

    ; System V x86_64 ABI
    ; 1번째 인자: RDI
    ; 2번째 인자: RSI
    mov edi, dword [mb2_magic]
    mov esi, dword [mb2_info]

    call kernel_main

hang64:
    hlt
    jmp hang64


section .data
align 4

mb2_magic:
    dd 0

mb2_info:
    dd 0


section .bss
align 4096

p4_table:
    resq 512

p3_table:
    resq 512

p2_table:
    resq 512

align 16

stack_bottom:
    resb 16384

stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits