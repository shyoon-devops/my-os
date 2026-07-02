#include "console.h"
#include "device.h"
#include "print.h"
#include "spinlock.h"
#include "tty.h"
#include "types.h"
#include "wait.h"

#define TTY_BUFFER_SIZE 256

static tty_key_t tty_buffer[TTY_BUFFER_SIZE];

static volatile u32 tty_head = 0;
static volatile u32 tty_tail = 0;
static volatile u32 tty_dropped = 0;

static spinlock_t tty_lock;
static wait_queue_t tty_wait_queue;

static device_t* tty_device = 0;

static u32 next_index(u32 index) {
    return (index + 1) % TTY_BUFFER_SIZE;
}

/*
 * IRQ가 꺼진 상태에서만 호출한다.
 *
 * 지금은 단일 CPU toy kernel이므로 IRQ만 꺼도 keyboard IRQ와
 * head/tail 경합을 막을 수 있다.
 */
static u32 tty_read_key_irq_disabled(tty_key_t* out) {
    if (!out) {
        return 0;
    }

    if (tty_tail == tty_head) {
        return 0;
    }

    *out = tty_buffer[tty_tail];
    tty_tail = next_index(tty_tail);

    return 1;
}

void tty_init(void) {
    spinlock_init(&tty_lock);
    wait_queue_init(&tty_wait_queue);

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

    /*
     * 실제로 키가 들어갔을 때만 기다리는 task를 깨운다.
     *
     * keyboard IRQ context에서 호출될 수 있으므로
     * wake 함수는 짧게 끝나야 한다.
     */
    if (result) {
        wait_queue_wake_one(&tty_wait_queue);
    }

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

void tty_read_key_blocking(tty_key_t* out) {
    if (!out) {
        return;
    }

    for (;;) {
        /*
         * lost wakeup 방지:
         *
         *   1. IRQ를 끈다.
         *   2. 버퍼를 확인한다.
         *   3. 비어 있으면 wait queue에 등록한다.
         *   4. 그 뒤 IRQ를 복구하고 scheduler로 전환한다.
         *
         * 이 순서가 중요하다.
         *
         * "비었음 확인"과 "잠들기 등록" 사이에 키보드 IRQ가 끼어들면
         * wakeup을 놓칠 수 있기 때문이다.
         */
        u64 flags = irq_save();

        if (tty_read_key_irq_disabled(out)) {
            irq_restore(flags);
            return;
        }

        wait_queue_block_irqrestore(&tty_wait_queue, flags);

        /*
         * 여기로 돌아왔다는 것은 keyboard IRQ 등으로 인해
         * WAITING -> READY가 되었고, scheduler가 이 task를 다시 실행했다는 뜻이다.
         *
         * 바로 반환하지 않고 다시 루프를 돈다.
         * 이유:
         *   여러 task가 같은 wait queue에서 깨어날 수도 있고,
         *   깨어났다고 해서 반드시 읽을 데이터가 남아 있다고 보장할 수는 없기 때문이다.
         */
    }
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
