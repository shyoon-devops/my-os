/*
 * Phase 10-E userland init.
 *
 * Keep this file as top-level assembly so the first executed user ELF
 * has no C ABI/prologue ambiguity. The kernel enters _start directly.
 */

__asm__(
".global _start\n"
"_start:\n"
"    mov $1, %rax\n"
"    mov $1, %rdi\n"
"    lea init_message(%rip), %rsi\n"
"    mov $(init_message_end - init_message), %rdx\n"
"    syscall\n"
"    mov $60, %rax\n"
"    xor %rdi, %rdi\n"
"    syscall\n"
"1:\n"
"    pause\n"
"    jmp 1b\n"
".section .rodata\n"
"init_message:\n"
"    .ascii \"hello from /bin/init via syscall\\n\"\n"
"init_message_end:\n"
".section .text\n"
);
