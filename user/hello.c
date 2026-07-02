#include "syscall.h"

__asm__(
".global _start\n"
"_start:\n"
"    xor %rbp, %rbp\n"
"    call hello_main\n"
"    mov %rax, %rdi\n"
"    mov $60, %rax\n"
"    syscall\n"
"1:\n"
"    pause\n"
"    jmp 1b\n"
);

static u64 user_strlen(const char* s) {
    u64 len = 0;

    if (!s) {
        return 0;
    }

    while (s[len]) {
        len++;
    }

    return len;
}

static void puts_user(const char* s) {
    user_write(USER_FD_STDOUT, s, user_strlen(s));
}

u64 hello_main(void) {
    puts_user("/bin/hello: hello from a second user program\n");
    puts_user("/bin/hello: returning exit code 7\n");

    return 7;
}
