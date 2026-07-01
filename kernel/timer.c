#include "command.h"
#include "console.h"
#include "device.h"
#include "heap.h"
#include "pit.h"
#include "print.h"
#include "spinlock.h"
#include "timer.h"
#include "types.h"
#include "workqueue.h"

typedef struct timer_item {
    u32 active;
    u64 due_tick;
    work_func_t func;
    void* arg;
    u64 seq;
} timer_item_t;

typedef struct due_work {
    work_func_t func;
    void* arg;
    u64 seq;
} due_work_t;

static timer_item_t timer_items[TIMER_MAX_ITEMS];

static spinlock_t timer_lock;

static u64 timer_next_seq = 1;
static u64 timer_fired = 0;
static u64 timer_failed = 0;

static device_t* timer_device = 0;

static u64 timer_strlen(const char* s) {
    u64 len = 0;

    if (!s) {
        return 0;
    }

    while (s[len]) {
        len++;
    }

    return len;
}

static char* timer_strdup(const char* s) {
    if (!s) {
        return 0;
    }

    u64 len = timer_strlen(s);
    char* copy = (char*)kmalloc(len + 1);

    if (!copy) {
        return 0;
    }

    for (u64 i = 0; i < len; i++) {
        copy[i] = s[i];
    }

    copy[len] = '\0';

    return copy;
}

void timer_init(void) {
    spinlock_init(&timer_lock);

    for (u32 i = 0; i < TIMER_MAX_ITEMS; i++) {
        timer_items[i].active = 0;
        timer_items[i].due_tick = 0;
        timer_items[i].func = 0;
        timer_items[i].arg = 0;
        timer_items[i].seq = 0;
    }

    timer_next_seq = 1;
    timer_fired = 0;
    timer_failed = 0;

    timer_device = device_register(
        "timer0",
        DEVICE_TYPE_TIMER,
        DEVICE_STATE_READY,
        "kernel-timer"
    );

    (void)timer_device;

    print_color("Kernel timer subsystem initialized\n", COLOR_GREEN_ON_BLACK);
}

u32 timer_schedule_ticks(u64 delay_ticks, work_func_t func, void* arg) {
    if (!func) {
        return 0;
    }

    /*
     * delay 0도 허용은 하지만, 최소 다음 poll에서 실행되게 1 tick으로 보정.
     */
    if (delay_ticks == 0) {
        delay_ticks = 1;
    }

    u64 flags;
    u32 result = 0;
    u64 now = pit_get_ticks();

    spin_lock_irqsave(&timer_lock, &flags);

    for (u32 i = 0; i < TIMER_MAX_ITEMS; i++) {
        if (!timer_items[i].active) {
            timer_items[i].active = 1;
            timer_items[i].due_tick = now + delay_ticks;
            timer_items[i].func = func;
            timer_items[i].arg = arg;
            timer_items[i].seq = timer_next_seq;

            timer_next_seq++;
            result = 1;
            break;
        }
    }

    if (!result) {
        timer_failed++;
    }

    spin_unlock_irqrestore(&timer_lock, flags);

    return result;
}

u32 timer_poll(void) {
    due_work_t due_items[TIMER_MAX_ITEMS];
    u32 due_count = 0;
    u64 now = pit_get_ticks();

    u64 flags;

    spin_lock_irqsave(&timer_lock, &flags);

    for (u32 i = 0; i < TIMER_MAX_ITEMS; i++) {
        if (timer_items[i].active && now >= timer_items[i].due_tick) {
            due_items[due_count].func = timer_items[i].func;
            due_items[due_count].arg = timer_items[i].arg;
            due_items[due_count].seq = timer_items[i].seq;
            due_count++;

            timer_items[i].active = 0;
            timer_items[i].due_tick = 0;
            timer_items[i].func = 0;
            timer_items[i].arg = 0;
            timer_items[i].seq = 0;
        }
    }

    spin_unlock_irqrestore(&timer_lock, flags);

    u64 fired_add = 0;
    u64 failed_add = 0;

    for (u32 i = 0; i < due_count; i++) {
        if (workqueue_schedule(due_items[i].func, due_items[i].arg)) {
            fired_add++;
        } else {
            failed_add++;
        }
    }

    if (fired_add || failed_add) {
        spin_lock_irqsave(&timer_lock, &flags);
        timer_fired += fired_add;
        timer_failed += failed_add;
        spin_unlock_irqrestore(&timer_lock, flags);
    }

    return due_count;
}

u32 timer_pending_count(void) {
    u64 flags;
    u32 count = 0;

    spin_lock_irqsave(&timer_lock, &flags);

    for (u32 i = 0; i < TIMER_MAX_ITEMS; i++) {
        if (timer_items[i].active) {
            count++;
        }
    }

    spin_unlock_irqrestore(&timer_lock, flags);

    return count;
}

u64 timer_fired_count(void) {
    u64 flags;
    u64 count;

    spin_lock_irqsave(&timer_lock, &flags);
    count = timer_fired;
    spin_unlock_irqrestore(&timer_lock, flags);

    return count;
}

u64 timer_failed_count(void) {
    u64 flags;
    u64 count;

    spin_lock_irqsave(&timer_lock, &flags);
    count = timer_failed;
    spin_unlock_irqrestore(&timer_lock, flags);

    return count;
}

/*
 * Test commands
 */

static void delaytest_work(void* arg) {
    char* message = (char*)arg;

    /*
     * 현재 shell prompt 위에 비동기 출력이 끼어들 수 있으므로
     * 앞에 개행을 넣는다. 나중에 console redraw가 생기면 더 깔끔하게 처리 가능.
     */
    print("\n");
    print_color("[timer]\n", COLOR_YELLOW_ON_BLACK);

    print("delayed work executed at tick ");
    print_dec64(pit_get_ticks());
    print(": ");

    if (message) {
        print(message);
    } else {
        print("(null)");
    }

    print("\n");

    if (message) {
        kfree(message);
    }
}

static void cmd_delaytest(const char* args) {
    const char* message = args;

    if (!message || message[0] == '\0') {
        message = "hello from delayed work";
    }

    char* copy = timer_strdup(message);

    if (!copy) {
        print_color("delaytest failed: no memory\n", COLOR_RED_ON_BLACK);
        return;
    }

    /*
     * 현재 PIT를 100Hz로 초기화하므로 100 ticks는 대략 1초.
     */
    if (timer_schedule_ticks(100, delaytest_work, copy)) {
        print("delayed work scheduled after 100 ticks\n");
    } else {
        kfree(copy);
        print_color("delaytest failed: timer queue full\n", COLOR_RED_ON_BLACK);
    }
}

static void cmd_timerstats(const char* args) {
    (void)args;

    print("current ticks  = ");
    print_dec64(pit_get_ticks());
    print("\n");

    print("timer pending  = ");
    print_dec64(timer_pending_count());
    print("\n");

    print("timer fired    = ");
    print_dec64(timer_fired_count());
    print("\n");

    print("timer failed   = ");
    print_dec64(timer_failed_count());
    print("\n");
}

void timer_register_builtin_commands(void) {
    command_register("delaytest", "schedule delayed work after 100 ticks", cmd_delaytest);
    command_register("timerstats", "show kernel timer stats", cmd_timerstats);
}
