#include "console.h"
#include "device.h"
#include "print.h"
#include "spinlock.h"
#include "tty.h"
#include "types.h"

#define TTY_BUFFER_SIZE 256

static tty_key_t tty_buffer[TTY_BUFFER_SIZE];

static volatile u32 tty_head = 0;
static volatile u32 tty_tail = 0;
static volatile u32 tty_dropped = 0;

static spinlock_t tty_lock;

static device_t* tty_device = 0;

static u32 next_index(u32 index) {
    return (index + 1) % TTY_BUFFER_SIZE;
}

void tty_init(void) {
    spinlock_init(&tty_lock);

    tty_head = 0;
    tty_tail = 0;
    tty_dropped = 0;

    tty_device = device_register(
        "tty0",
        DEVICE_TYPE_TTY,
        DEVICE_STATE_READY,
        "kernel-tty"
    );

    (void)tty_device;

    print_color("TTY input buffer initialized\n", COLOR_GREEN_ON_BLACK);
}

u32 tty_push_key(tty_key_t key) {
    u64 flags;
    u32 result = 1;

    spin_lock_irqsave(&tty_lock, &flags);

    u32 next = next_index(tty_head);

    if (next == tty_tail) {
        tty_dropped++;
        result = 0;
    } else {
        tty_buffer[tty_head] = key;
        tty_head = next;
    }

    spin_unlock_irqrestore(&tty_lock, flags);

    return result;
}

u32 tty_push_char(char c) {
    return tty_push_key((tty_key_t)(u8)c);
}

u32 tty_read_key(tty_key_t* out) {
    if (!out) {
        return 0;
    }

    u64 flags;
    u32 result = 1;

    spin_lock_irqsave(&tty_lock, &flags);

    if (tty_tail == tty_head) {
        result = 0;
    } else {
        *out = tty_buffer[tty_tail];
        tty_tail = next_index(tty_tail);
    }

    spin_unlock_irqrestore(&tty_lock, flags);

    return result;
}

u32 tty_available(void) {
    u64 flags;
    u32 available;

    spin_lock_irqsave(&tty_lock, &flags);

    if (tty_head >= tty_tail) {
        available = tty_head - tty_tail;
    } else {
        available = TTY_BUFFER_SIZE - tty_tail + tty_head;
    }

    spin_unlock_irqrestore(&tty_lock, flags);

    return available;
}