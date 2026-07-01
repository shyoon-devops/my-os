#include "command.h"
#include "console.h"
#include "panic.h"
#include "print.h"
#include "types.h"

static volatile u32 panic_active = 0;

void kernel_panic_halt(void) {
    __asm__ volatile ("cli");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void panic_print_location(const char* file, u64 line) {
    if (!file) {
        return;
    }

    print("at      = ");
    print(file);
    print(":");
    print_dec64(line);
    print("\n");
}

static void panic_begin(void) {
    __asm__ volatile ("cli");

    if (panic_active) {
        print_color("\n[RECURSIVE PANIC]\n", COLOR_RED_ON_BLACK);
        print("panic while already handling panic\n");
        kernel_panic_halt();
    }

    panic_active = 1;
}

void kernel_panic(const char* message) {
    kernel_panic_at(message, 0, 0);
}

void kernel_panic_at(const char* message, const char* file, u64 line) {
    panic_begin();

    print_color("\n[KERNEL PANIC]\n", COLOR_RED_ON_BLACK);

    print("message = ");

    if (message) {
        print(message);
    } else {
        print("(null)");
    }

    print("\n");

    panic_print_location(file, line);

    print_color("system halted\n", COLOR_RED_ON_BLACK);

    kernel_panic_halt();
}

void kernel_panic_assert(const char* expr, const char* file, u64 line) {
    panic_begin();

    print_color("\n[KERNEL PANIC]\n", COLOR_RED_ON_BLACK);

    print("assertion failed: ");

    if (expr) {
        print(expr);
    } else {
        print("(null)");
    }

    print("\n");

    panic_print_location(file, line);

    print_color("system halted\n", COLOR_RED_ON_BLACK);

    kernel_panic_halt();
}

static void cmd_panic(const char* args) {
    if (!args || args[0] == '\0') {
        KPANIC("manual panic");
    }

    KPANIC(args);
}

static void cmd_asserttest(const char* args) {
    (void)args;

    KASSERT(1 == 0);
}

void panic_register_builtin_commands(void) {
    command_register("panic", "trigger kernel panic", cmd_panic);
    command_register("asserttest", "trigger kernel assertion failure", cmd_asserttest);
}