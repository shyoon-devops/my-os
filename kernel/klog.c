#include "command.h"
#include "console.h"
#include "klog.h"
#include "print.h"
#include "serial.h"
#include "spinlock.h"
#include "types.h"

static char klog_buffer[KLOG_BUFFER_SIZE];

static u32 klog_head = 0;
static u32 klog_count = 0;
static u32 klog_initialized = 0;

static spinlock_t klog_lock;

static u32 klog_next_index(u32 index) {
    return (index + 1) % KLOG_BUFFER_SIZE;
}

void klog_init(void) {
    spinlock_init(&klog_lock);

    klog_head = 0;
    klog_count = 0;
    klog_initialized = 1;

    print_color("Kernel log buffer initialized\n", COLOR_GREEN_ON_BLACK);
}

void klog_write_char(char c) {
    if (!klog_initialized) {
        return;
    }

    u64 flags;

    spin_lock_irqsave(&klog_lock, &flags);

    klog_buffer[klog_head] = c;
    klog_head = klog_next_index(klog_head);

    if (klog_count < KLOG_BUFFER_SIZE) {
        klog_count++;
    }

    spin_unlock_irqrestore(&klog_lock, flags);
}

void klog_clear(void) {
    u64 flags;

    spin_lock_irqsave(&klog_lock, &flags);

    klog_head = 0;
    klog_count = 0;

    spin_unlock_irqrestore(&klog_lock, flags);
}

u32 klog_size(void) {
    u64 flags;
    u32 size;

    spin_lock_irqsave(&klog_lock, &flags);
    size = klog_count;
    spin_unlock_irqrestore(&klog_lock, flags);

    return size;
}

u32 klog_capacity(void) {
    return KLOG_BUFFER_SIZE;
}

void klog_dump(void) {
    if (!klog_initialized) {
        return;
    }

    u64 flags;

    spin_lock_irqsave(&klog_lock, &flags);

    /*
     * dump는 print()를 쓰지 않는다.
     *
     * print()를 쓰면 dmesg 출력 자체가 다시 klog에 들어가서
     * 로그가 중복으로 계속 늘어난다.
     */
    u32 start;

    if (klog_count == KLOG_BUFFER_SIZE) {
        start = klog_head;
    } else {
        start = 0;
    }

    u32 index = start;

    for (u32 i = 0; i < klog_count; i++) {
        char c = klog_buffer[index];

        console_put_char(c);
        serial_write_char(c);

        index = klog_next_index(index);
    }

    spin_unlock_irqrestore(&klog_lock, flags);
}

static void cmd_dmesg(const char* args) {
    (void)args;

    klog_dump();
}

static void cmd_dmesg_clear(const char* args) {
    (void)args;

    klog_clear();
    print("klog cleared\n");
}

static void cmd_dmesg_info(const char* args) {
    (void)args;

    print("klog size     = ");
    print_dec64(klog_size());
    print(" bytes\n");

    print("klog capacity = ");
    print_dec64(klog_capacity());
    print(" bytes\n");
}

void klog_register_builtin_commands(void) {
    command_register("dmesg", "dump kernel log buffer", cmd_dmesg);
    command_register("dmesgclear", "clear kernel log buffer", cmd_dmesg_clear);
    command_register("dmesginfo", "show kernel log buffer info", cmd_dmesg_info);
}