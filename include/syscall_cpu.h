#ifndef MY_OS_SYSCALL_CPU_H
#define MY_OS_SYSCALL_CPU_H

#include "types.h"

void syscall_cpu_init(void);

u32 syscall_cpu_enabled(void);
u64 syscall_cpu_entry_address(void);
u64 syscall_cpu_efer_before(void);
u64 syscall_cpu_efer_after(void);
u64 syscall_cpu_star(void);
u64 syscall_cpu_lstar(void);
u64 syscall_cpu_fmask(void);

void syscall_cpu_register_builtin_commands(void);

#endif
