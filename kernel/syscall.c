#include "command.h"
#include "fd.h"
#include "print.h"
#include "syscall.h"
#include "types.h"

#define SYSCALL_TABLE_SIZE 16

static syscall_entry_t syscall_table[SYSCALL_TABLE_SIZE];
static u32 syscall_count = 0;

static u32 syscall_initialized = 0;

static u64 sys_read(
    u64 arg0,
    u64 arg1,
    u64 arg2,
    u64 arg3,
    u64 arg4,
    u64 arg5
) {
    (void)arg3;
    (void)arg4;
    (void)arg5;

    s32 fd = (s32)arg0;
    void* buffer = (void*)arg1;
    u64 size = arg2;

    if (!buffer || size == 0) {
        return SYSCALL_ERR_FAULT;
    }

    s64 result = fd_read(fd, buffer, size);

    if (result < 0) {
        return SYSCALL_ERR_BADFD;
    }

    return (u64)result;
}

static u64 sys_write(
    u64 arg0,
    u64 arg1,
    u64 arg2,
    u64 arg3,
    u64 arg4,
    u64 arg5
) {
    (void)arg3;
    (void)arg4;
    (void)arg5;

    s32 fd = (s32)arg0;
    const void* buffer = (const void*)arg1;
    u64 size = arg2;

    if (!buffer || size == 0) {
        return SYSCALL_ERR_FAULT;
    }

    s64 result = fd_write(fd, buffer, size);

    if (result < 0) {
        return SYSCALL_ERR_BADFD;
    }

    return (u64)result;
}

static u64 sys_getpid(
    u64 arg0,
    u64 arg1,
    u64 arg2,
    u64 arg3,
    u64 arg4,
    u64 arg5
) {
    (void)arg0;
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;

    /*
     * 아직 user process가 없으므로 고정 PID를 반환한다.
     * Phase 11 user process / exec에서 실제 process id로 교체한다.
     */
    return 1;
}

static u64 sys_exit(
    u64 arg0,
    u64 arg1,
    u64 arg2,
    u64 arg3,
    u64 arg4,
    u64 arg5
) {
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;

    /*
     * 아직 user process가 없으므로 실제 종료는 하지 않는다.
     * exit code만 반환해서 syscall path 테스트용으로 사용한다.
     */
    return arg0;
}

static void syscall_register(u64 number, const char* name, syscall_handler_t handler) {
    if (syscall_count >= SYSCALL_TABLE_SIZE) {
        return;
    }

    syscall_table[syscall_count].number = number;
    syscall_table[syscall_count].name = name;
    syscall_table[syscall_count].handler = handler;

    syscall_count++;
}

void syscall_init(void) {
    syscall_count = 0;

    syscall_register(SYS_READ, "read", sys_read);
    syscall_register(SYS_WRITE, "write", sys_write);
    syscall_register(SYS_GETPID, "getpid", sys_getpid);
    syscall_register(SYS_EXIT, "exit", sys_exit);

    syscall_initialized = 1;

    print_color("Syscall dispatch table initialized\n", COLOR_GREEN_ON_BLACK);
}

u64 syscall_dispatch(
    u64 number,
    u64 arg0,
    u64 arg1,
    u64 arg2,
    u64 arg3,
    u64 arg4,
    u64 arg5
) {
    if (!syscall_initialized) {
        return SYSCALL_ERR_INVAL;
    }

    for (u32 i = 0; i < syscall_count; i++) {
        if (syscall_table[i].number == number) {
            if (!syscall_table[i].handler) {
                return SYSCALL_ERR_INVAL;
            }

            return syscall_table[i].handler(
                arg0,
                arg1,
                arg2,
                arg3,
                arg4,
                arg5
            );
        }
    }

    return SYSCALL_ERR_INVAL;
}

static void print_syscall_result(u64 value) {
    if (value == SYSCALL_ERR_INVAL) {
        print("ERR_INVAL");
        return;
    }

    if (value == SYSCALL_ERR_BADFD) {
        print("ERR_BADFD");
        return;
    }

    if (value == SYSCALL_ERR_FAULT) {
        print("ERR_FAULT");
        return;
    }

    print_dec64(value);
}

static void cmd_syscallinfo(const char* args) {
    (void)args;

    print("syscall initialized = ");
    print_dec64(syscall_initialized);
    print("\n");

    print("syscall count = ");
    print_dec64(syscall_count);
    print("\n");

    for (u32 i = 0; i < syscall_count; i++) {
        print("  ");
        print_dec64(syscall_table[i].number);
        print(" ");
        print(syscall_table[i].name);
        print("\n");
    }
}

static void cmd_syscalltest(const char* args) {
    (void)args;

    const char* message = "hello through SYS_write\n";

    u64 written = syscall_dispatch(
        SYS_WRITE,
        FD_STDOUT,
        (u64)message,
        24,
        0,
        0,
        0
    );

    print("SYS_write returned ");
    print_syscall_result(written);
    print("\n");

    u64 pid = syscall_dispatch(
        SYS_GETPID,
        0,
        0,
        0,
        0,
        0,
        0
    );

    print("SYS_getpid returned ");
    print_syscall_result(pid);
    print("\n");

    u64 exit_code = syscall_dispatch(
        SYS_EXIT,
        7,
        0,
        0,
        0,
        0,
        0
    );

    print("SYS_exit returned ");
    print_syscall_result(exit_code);
    print("\n");
}

void syscall_register_builtin_commands(void) {
    command_register("syscallinfo", "show syscall dispatch table", cmd_syscallinfo);
    command_register("syscalltest", "test syscall dispatcher from kernel", cmd_syscalltest);
}
