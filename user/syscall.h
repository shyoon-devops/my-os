#ifndef MY_OS_USER_SYSCALL_H
#define MY_OS_USER_SYSCALL_H

typedef unsigned long long u64;
typedef long long s64;

#define USER_SYS_WRITE  1ULL
#define USER_SYS_GETPID 39ULL
#define USER_SYS_EXIT   60ULL

#define USER_FD_STDIN   0ULL
#define USER_FD_STDOUT  1ULL
#define USER_FD_STDERR  2ULL

static inline u64 user_syscall0(u64 number) {
    u64 ret;

    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(number)
        : "rcx", "r11", "memory"
    );

    return ret;
}

static inline u64 user_syscall1(u64 number, u64 arg0) {
    u64 ret;

    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(number), "D"(arg0)
        : "rcx", "r11", "memory"
    );

    return ret;
}

static inline u64 user_syscall3(u64 number, u64 arg0, u64 arg1, u64 arg2) {
    u64 ret;

    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(number), "D"(arg0), "S"(arg1), "d"(arg2)
        : "rcx", "r11", "memory"
    );

    return ret;
}

static inline s64 user_write(u64 fd, const void* buffer, u64 size) {
    return (s64)user_syscall3(USER_SYS_WRITE, fd, (u64)buffer, size);
}

static inline u64 user_getpid(void) {
    return user_syscall0(USER_SYS_GETPID);
}

static inline void user_exit(u64 code) {
    user_syscall1(USER_SYS_EXIT, code);

    for (;;) {
        __asm__ volatile ("pause");
    }
}

#endif
