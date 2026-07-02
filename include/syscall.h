#ifndef MY_OS_SYSCALL_H
#define MY_OS_SYSCALL_H

#include "types.h"

#define SYS_READ    0
#define SYS_WRITE   1
#define SYS_GETPID  39
#define SYS_EXIT    60

#define SYSCALL_MAX_ARGS 6

#define SYSCALL_ERR_INVAL ((u64)-1)
#define SYSCALL_ERR_BADFD ((u64)-2)
#define SYSCALL_ERR_FAULT ((u64)-3)

typedef u64 (*syscall_handler_t)(
    u64 arg0,
    u64 arg1,
    u64 arg2,
    u64 arg3,
    u64 arg4,
    u64 arg5
);

typedef struct syscall_entry {
    u64 number;
    const char* name;
    syscall_handler_t handler;
} syscall_entry_t;

void syscall_init(void);

u64 syscall_dispatch(
    u64 number,
    u64 arg0,
    u64 arg1,
    u64 arg2,
    u64 arg3,
    u64 arg4,
    u64 arg5
);

void syscall_register_builtin_commands(void);

#endif
