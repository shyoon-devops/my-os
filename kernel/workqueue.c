#include "command.h"
#include "console.h"
#include "device.h"
#include "heap.h"
#include "print.h"
#include "spinlock.h"
#include "types.h"
#include "workqueue.h"

#define WORKQUEUE_SIZE 64

typedef struct work_item {
    work_func_t func;
    void* arg;
    u64 seq;
} work_item_t;

static work_item_t work_items[WORKQUEUE_SIZE];

static u32 work_head = 0;
static u32 work_tail = 0;
static u32 work_count = 0;

static u64 work_next_seq = 1;
static u64 work_executed = 0;
static u64 work_dropped = 0;

static spinlock_t workqueue_lock;

static device_t* workqueue_device = 0;

static u32 next_index(u32 index) {
    return (index + 1) % WORKQUEUE_SIZE;
}

static u64 wq_strlen(const char* s) {
    u64 len = 0;

    if (!s) {
        return 0;
    }

    while (s[len]) {
        len++;
    }

    return len;
}

static char* wq_strdup(const char* s) {
    if (!s) {
        return 0;
    }

    u64 len = wq_strlen(s);
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

void workqueue_init(void) {
    spinlock_init(&workqueue_lock);

    work_head = 0;
    work_tail = 0;
    work_count = 0;

    work_next_seq = 1;
    work_executed = 0;
    work_dropped = 0;

    workqueue_device = device_register(
        "workqueue0",
        DEVICE_TYPE_VIRTUAL,
        DEVICE_STATE_READY,
        "kernel-workqueue"
    );

    (void)workqueue_device;

    print_color("Kernel workqueue initialized\n", COLOR_GREEN_ON_BLACK);
}

u32 workqueue_schedule(work_func_t func, void* arg) {
    if (!func) {
        return 0;
    }

    u64 flags;
    u32 result = 1;

    spin_lock_irqsave(&workqueue_lock, &flags);

    if (work_count >= WORKQUEUE_SIZE) {
        work_dropped++;
        result = 0;
    } else {
        work_items[work_head].func = func;
        work_items[work_head].arg = arg;
        work_items[work_head].seq = work_next_seq;

        work_next_seq++;
        work_head = next_index(work_head);
        work_count++;
    }

    spin_unlock_irqrestore(&workqueue_lock, flags);

    return result;
}

static u32 workqueue_pop(work_item_t* out) {
    if (!out) {
        return 0;
    }

    u64 flags;
    u32 result = 1;

    spin_lock_irqsave(&workqueue_lock, &flags);

    if (work_count == 0) {
        result = 0;
    } else {
        *out = work_items[work_tail];

        work_items[work_tail].func = 0;
        work_items[work_tail].arg = 0;
        work_items[work_tail].seq = 0;

        work_tail = next_index(work_tail);
        work_count--;
    }

    spin_unlock_irqrestore(&workqueue_lock, flags);

    return result;
}

u32 workqueue_run_pending(u32 max_items) {
    u32 ran = 0;
    work_item_t item;

    if (max_items == 0 || max_items > WORKQUEUE_SIZE) {
        max_items = WORKQUEUE_SIZE;
    }

    while (ran < max_items && workqueue_pop(&item)) {
        item.func(item.arg);

        u64 flags;
        spin_lock_irqsave(&workqueue_lock, &flags);
        work_executed++;
        spin_unlock_irqrestore(&workqueue_lock, flags);

        ran++;
    }

    return ran;
}

u32 workqueue_pending_count(void) {
    u64 flags;
    u32 count;

    spin_lock_irqsave(&workqueue_lock, &flags);
    count = work_count;
    spin_unlock_irqrestore(&workqueue_lock, flags);

    return count;
}

u64 workqueue_executed_count(void) {
    u64 flags;
    u64 count;

    spin_lock_irqsave(&workqueue_lock, &flags);
    count = work_executed;
    spin_unlock_irqrestore(&workqueue_lock, flags);

    return count;
}

u64 workqueue_dropped_count(void) {
    u64 flags;
    u64 count;

    spin_lock_irqsave(&workqueue_lock, &flags);
    count = work_dropped;
    spin_unlock_irqrestore(&workqueue_lock, flags);

    return count;
}

/*
 * Test commands
 */

static void wqtest_work(void* arg) {
    char* message = (char*)arg;

    print_color("[workqueue]\n", COLOR_YELLOW_ON_BLACK);

    print("deferred work executed: ");

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

static void cmd_wqtest(const char* args) {
    const char* message = args;

    if (!message || message[0] == '\0') {
        message = "hello from deferred work";
    }

    /*
     * args는 shell line_buffer를 가리키므로,
     * workqueue에서 나중에 실행될 때는 이미 덮일 수 있다.
     * 그래서 heap에 복사해서 넘긴다.
     */
    char* copy = wq_strdup(message);

    if (!copy) {
        print_color("work schedule failed: no memory\n", COLOR_RED_ON_BLACK);
        return;
    }

    if (workqueue_schedule(wqtest_work, copy)) {
        print("work scheduled\n");
    } else {
        kfree(copy);
        print_color("work schedule failed\n", COLOR_RED_ON_BLACK);
    }
}

static void cmd_wqstats(const char* args) {
    (void)args;

    print("workqueue pending  = ");
    print_dec64(workqueue_pending_count());
    print("\n");

    print("workqueue executed = ");
    print_dec64(workqueue_executed_count());
    print("\n");

    print("workqueue dropped  = ");
    print_dec64(workqueue_dropped_count());
    print("\n");
}

static void cmd_wqrun(const char* args) {
    (void)args;

    u32 ran = workqueue_run_pending(0);

    print("work items executed = ");
    print_dec64(ran);
    print("\n");
}

void workqueue_register_builtin_commands(void) {
    command_register("wqtest", "schedule deferred work", cmd_wqtest);
    command_register("wqstats", "show workqueue stats", cmd_wqstats);
    command_register("wqrun", "run pending work immediately", cmd_wqrun);
}
