#include "syscall.h"

typedef unsigned short u16;

__asm__(
".global _start\n"
"_start:\n"
"    xor %rbp, %rbp\n"
"    call readkey_main\n"
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

static void putu_user(u64 value) {
    char buffer[32];
    u64 index = sizeof(buffer);

    if (value == 0) {
        user_write(USER_FD_STDOUT, "0", 1);
        return;
    }

    while (value > 0 && index > 0) {
        index--;
        buffer[index] = (char)('0' + (value % 10));
        value /= 10;
    }

    user_write(USER_FD_STDOUT, &buffer[index], sizeof(buffer) - index);
}

u64 readkey_main(void) {
    puts_user("/bin/readkey: waiting for one key...\n");

    u16 key = 0;
    s64 read = user_read(USER_FD_STDIN, &key, sizeof(key));

    puts_user("/bin/readkey: read bytes = ");
    putu_user((u64)read);
    puts_user("\n");

    puts_user("/bin/readkey: key code = ");
    putu_user((u64)key);
    puts_user("\n");

    if (key >= 0x20 && key <= 0x7e) {
        char c = (char)key;
        puts_user("/bin/readkey: printable = ");
        user_write(USER_FD_STDOUT, &c, 1);
        puts_user("\n");
    }

    return (u64)(key & 0xff);
}
