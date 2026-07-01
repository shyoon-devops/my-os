#ifndef GO_OS_PANIC_H
#define GO_OS_PANIC_H

#include "types.h"

void kernel_panic(const char* message);
void kernel_panic_at(const char* message, const char* file, u64 line);
void kernel_panic_assert(const char* expr, const char* file, u64 line);
void kernel_panic_halt(void);

void panic_register_builtin_commands(void);

#define KPANIC(message) \
    kernel_panic_at((message), __FILE__, (u64)__LINE__)

#define KASSERT(expr) \
    do { \
        if (!(expr)) { \
            kernel_panic_assert(#expr, __FILE__, (u64)__LINE__); \
        } \
    } while (0)

#endif