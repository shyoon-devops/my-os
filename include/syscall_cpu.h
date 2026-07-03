#ifndef MY_OS_SYSCALL_CPU_H
#define MY_OS_SYSCALL_CPU_H

#include "types.h"

void syscall_cpu_init(void);

void syscall_cpu_register_builtin_commands(void);

#endif
