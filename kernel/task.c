#include "command.h"
#include "console.h"
#include "device.h"
#include "heap.h"
#include "panic.h"
#include "pit.h"
#include "print.h"
#include "spinlock.h"
#include "task.h"
#include "types.h"

#define TASK_STACK_SIZE 16384ULL
#define TASK_NAME_MAX   32

typedef struct task {
    u32 id;
    char* name;

    task_state_t state;
    u64 wake_tick;

    u64 rsp;

    void* stack;
    u64 stack_size;

    task_entry_t entry;
    void* arg;

    task_user_context_t user_context;

    struct task* next;
    struct task* wait_next;
} task_t;

extern void task_context_switch(u64* old_rsp, u64 new_rsp);
extern void task_entry_trampoline(void);

static spinlock_t task_lock;

static task_t* task_head = 0;
static task_t* task_tail = 0;
static task_t* current_task = 0;
static task_t* schedule_cursor = 0;

static u32 next_task_id = 1;
static u32 total_task_count = 0;

static u64 scheduler_rsp = 0;

static device_t* scheduler_device = 0;

static u64 task_strlen(const char* s) {
    u64 len = 0;

    if (!s) {
        return 0;
    }

    while (s[len]) {
        len++;
    }

    return len;
}

static char* task_strdup_limited(const char* s) {
    if (!s) {
        s = "task";
    }

    u64 len = task_strlen(s);

    if (len > TASK_NAME_MAX) {
        len = TASK_NAME_MAX;
    }

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

static u64 align_down_u64_local(u64 value, u64 align) {
    return value & ~(align - 1);
}

const char* task_state_name(task_state_t state) {
    switch (state) {
        case TASK_STATE_READY:
            return "ready";
        case TASK_STATE_RUNNING:
            return "running";
        case TASK_STATE_SLEEPING:
            return "sleeping";
        case TASK_STATE_WAITING:
            return "waiting";
        case TASK_STATE_DONE:
            return "done";
        case TASK_STATE_UNUSED:
            return "unused";
        default:
            return "unknown";
    }
}

void task_init(void) {
    spinlock_init(&task_lock);

    task_head = 0;
    task_tail = 0;
    current_task = 0;
    schedule_cursor = 0;

    next_task_id = 1;
    total_task_count = 0;

    scheduler_rsp = 0;

    scheduler_device = device_register(
        "sched0",
        DEVICE_TYPE_VIRTUAL,
        DEVICE_STATE_READY,
        "cooperative-scheduler"
    );

    (void)scheduler_device;

    print_color("Cooperative task scheduler initialized\n", COLOR_GREEN_ON_BLACK);
}

u32 task_create(const char* name, task_entry_t entry, void* arg) {
    if (!entry) {
        return 0;
    }

    task_t* task = (task_t*)kzalloc(sizeof(task_t));

    if (!task) {
        print_color("task_create failed: no memory for task\n", COLOR_RED_ON_BLACK);
        return 0;
    }

    task->name = task_strdup_limited(name);

    if (!task->name) {
        print_color("task_create failed: no memory for name\n", COLOR_RED_ON_BLACK);
        return 0;
    }

    void* stack = kmalloc(TASK_STACK_SIZE);

    if (!stack) {
        print_color("task_create failed: no memory for stack\n", COLOR_RED_ON_BLACK);
        return 0;
    }

    u64 stack_top = (u64)stack + TASK_STACK_SIZE;
    stack_top = align_down_u64_local(stack_top, 16);

    /*
     * task_context_switch()가 새 stack으로 전환하면 아래 순서대로 pop한다.
     *
     *   pop r15
     *   pop r14
     *   pop r13
     *   pop r12
     *   pop rbx
     *   pop rbp
     *   ret
     *
     * 그래서 초기 stack에 fake register frame을 만들어 둔다.
     */
    u64* sp = (u64*)stack_top;

    *(--sp) = (u64)task_entry_trampoline;
    *(--sp) = 0; /* rbp */
    *(--sp) = 0; /* rbx */
    *(--sp) = 0; /* r12 */
    *(--sp) = 0; /* r13 */
    *(--sp) = 0; /* r14 */
    *(--sp) = 0; /* r15 */

    task->id = next_task_id;
    next_task_id++;

    task->state = TASK_STATE_READY;
    task->wake_tick = 0;
    task->rsp = (u64)sp;

    task->stack = stack;
    task->stack_size = TASK_STACK_SIZE;

    task->entry = entry;
    task->arg = arg;

    task->next = 0;
    task->wait_next = 0;

    u64 flags;
    spin_lock_irqsave(&task_lock, &flags);

    if (!task_head) {
        task_head = task;
        task_tail = task;
    } else {
        task_tail->next = task;
        task_tail = task;
    }

    total_task_count++;

    spin_unlock_irqrestore(&task_lock, flags);

    return task->id;
}

static void task_wakeup_sleeping_locked(void) {
    u64 now = pit_get_ticks();

    task_t* current = task_head;

    while (current) {
        if (current->state == TASK_STATE_SLEEPING &&
            now >= current->wake_tick) {
            current->state = TASK_STATE_READY;
            current->wake_tick = 0;
        }

        current = current->next;
    }
}

static task_t* task_pick_next_locked(void) {
    if (!task_head) {
        return 0;
    }

    task_wakeup_sleeping_locked();

    task_t* start = 0;

    if (schedule_cursor && schedule_cursor->next) {
        start = schedule_cursor->next;
    } else {
        start = task_head;
    }

    task_t* current = start;

    do {
        if (current->state == TASK_STATE_READY) {
            schedule_cursor = current;
            return current;
        }

        if (current->next) {
            current = current->next;
        } else {
            current = task_head;
        }
    } while (current != start);

    return 0;
}

u32 task_run_once(void) {
    u64 flags;

    spin_lock_irqsave(&task_lock, &flags);

    task_t* next = task_pick_next_locked();

    if (!next) {
        spin_unlock_irqrestore(&task_lock, flags);
        return 0;
    }

    current_task = next;
    current_task->state = TASK_STATE_RUNNING;

    u64 next_rsp = current_task->rsp;

    spin_unlock_irqrestore(&task_lock, flags);

    task_context_switch(&scheduler_rsp, next_rsp);

    spin_lock_irqsave(&task_lock, &flags);

    current_task = 0;

    spin_unlock_irqrestore(&task_lock, flags);

    return 1;
}

void task_yield(void) {
    if (!current_task) {
        return;
    }

    u64 flags;

    spin_lock_irqsave(&task_lock, &flags);

    if (current_task->state == TASK_STATE_RUNNING) {
        current_task->state = TASK_STATE_READY;
    }

    u64* old_rsp = &current_task->rsp;
    u64 target_rsp = scheduler_rsp;

    spin_unlock_irqrestore(&task_lock, flags);

    task_context_switch(old_rsp, target_rsp);
}

void task_sleep_ticks(u64 ticks) {
    if (!current_task) {
        return;
    }

    if (ticks == 0) {
        task_yield();
        return;
    }

    u64 flags;

    spin_lock_irqsave(&task_lock, &flags);

    current_task->state = TASK_STATE_SLEEPING;
    current_task->wake_tick = pit_get_ticks() + ticks;

    u64* old_rsp = &current_task->rsp;
    u64 target_rsp = scheduler_rsp;

    spin_unlock_irqrestore(&task_lock, flags);

    task_context_switch(old_rsp, target_rsp);
}

void task_exit(void) {
    if (!current_task) {
        KPANIC("task_exit without current task");
    }

    u64 flags;

    spin_lock_irqsave(&task_lock, &flags);

    current_task->state = TASK_STATE_DONE;
    current_task->wake_tick = 0;

    u64* old_rsp = &current_task->rsp;
    u64 target_rsp = scheduler_rsp;

    spin_unlock_irqrestore(&task_lock, flags);

    task_context_switch(old_rsp, target_rsp);

    KPANIC("task_exit returned unexpectedly");
}

/*
 * ---- wait queue용 최소 primitive ----
 *
 * 지금은 단일 CPU + cooperative scheduler 기준이다.
 * 따라서 wait queue 쪽에서는 IRQ 차단으로 keyboard IRQ와의 경합을 막는다.
 */
struct task* task_current(void) {
    return current_task;
}

task_user_context_t* task_current_user_context(void) {
    if (!current_task) {
        return 0;
    }

    return &current_task->user_context;
}

/*
 * syscall_entry.asm의 SYS_exit 경로에서 IRQ가 꺼진 상태로 호출된다.
 * 단일 CPU + cli 상태이므로 lock 없이 current_task를 읽어도 안전하다.
 */
u64 task_user_saved_kernel_rsp(void) {
    if (!current_task) {
        return 0;
    }

    return current_task->user_context.saved_kernel_rsp;
}

void task_set_waiting(struct task* task) {
    if (!task) {
        return;
    }

    task->state = TASK_STATE_WAITING;
}

void task_set_ready(struct task* task) {
    if (!task) {
        return;
    }

    if (task->state == TASK_STATE_WAITING) {
        task->state = TASK_STATE_READY;
    }
}

struct task* task_wait_next(struct task* task) {
    if (!task) {
        return 0;
    }

    return task->wait_next;
}

void task_wait_set_next(struct task* task, struct task* next) {
    if (!task) {
        return;
    }

    task->wait_next = next;
}

void task_block_switch(void) {
    if (!current_task) {
        return;
    }

    u64 flags;

    spin_lock_irqsave(&task_lock, &flags);

    u64* old_rsp = &current_task->rsp;
    u64 target_rsp = scheduler_rsp;

    spin_unlock_irqrestore(&task_lock, flags);

    task_context_switch(old_rsp, target_rsp);
}

/*
 * task_entry_trampoline에서 호출된다.
 */
void task_trampoline(void) {
    if (!current_task) {
        KPANIC("task_trampoline without current task");
    }

    current_task->entry(current_task->arg);

    task_exit();
}

u32 task_count(void) {
    u64 flags;
    u32 count;

    spin_lock_irqsave(&task_lock, &flags);
    count = total_task_count;
    spin_unlock_irqrestore(&task_lock, flags);

    return count;
}

u32 task_ready_count(void) {
    u64 flags;
    u32 count = 0;

    spin_lock_irqsave(&task_lock, &flags);

    task_wakeup_sleeping_locked();

    task_t* current = task_head;

    while (current) {
        if (current->state == TASK_STATE_READY) {
            count++;
        }

        current = current->next;
    }

    spin_unlock_irqrestore(&task_lock, flags);

    return count;
}

u32 task_sleeping_count(void) {
    u64 flags;
    u32 count = 0;

    spin_lock_irqsave(&task_lock, &flags);

    task_wakeup_sleeping_locked();

    task_t* current = task_head;

    while (current) {
        if (current->state == TASK_STATE_SLEEPING) {
            count++;
        }

        current = current->next;
    }

    spin_unlock_irqrestore(&task_lock, flags);

    return count;
}

u32 task_waiting_count(void) {
    u64 flags;
    u32 count = 0;

    spin_lock_irqsave(&task_lock, &flags);

    task_t* current = task_head;

    while (current) {
        if (current->state == TASK_STATE_WAITING) {
            count++;
        }

        current = current->next;
    }

    spin_unlock_irqrestore(&task_lock, flags);

    return count;
}

void task_print_all(void) {
    u64 flags;

    spin_lock_irqsave(&task_lock, &flags);

    task_wakeup_sleeping_locked();

    print("tasks:\n");

    task_t* current = task_head;

    while (current) {
        print("  #");
        print_dec64(current->id);
        print(" ");

        print(current->name);

        u64 name_len = task_strlen(current->name);
        if (name_len < 12) {
            for (u64 i = name_len; i < 12; i++) {
                print(" ");
            }
        } else {
            print(" ");
        }

        print(" state=");
        print(task_state_name(current->state));

        if (current->state == TASK_STATE_SLEEPING) {
            print(" wake=");
            print_dec64(current->wake_tick);
        }

        print(" stack=");
        print_hex64((u64)current->stack);

        print(" rsp=");
        print_hex64(current->rsp);

        print("\n");

        current = current->next;
    }

    print("task count = ");
    print_dec64(total_task_count);
    print("\n");

    spin_unlock_irqrestore(&task_lock, flags);
}

static void demo_task(void* arg) {
    const char* name = (const char*)arg;

    for (u32 i = 0; i < 5; i++) {
        print("\n");
        print_color("[task]\n", COLOR_YELLOW_ON_BLACK);

        print("task ");
        print(name);
        print(" iteration ");
        print_dec64(i);
        print(" tick=");
        print_dec64(pit_get_ticks());
        print("\n");

        task_yield();
    }

    print("\n");
    print_color("[task]\n", COLOR_YELLOW_ON_BLACK);
    print("task ");
    print(name);
    print(" done\n");
}

static void sleepy_task(void* arg) {
    const char* name = (const char*)arg;

    for (u32 i = 0; i < 3; i++) {
        print("\n");
        print_color("[sleepy task]\n", COLOR_YELLOW_ON_BLACK);

        print("task ");
        print(name);
        print(" awake iteration ");
        print_dec64(i);
        print(" tick=");
        print_dec64(pit_get_ticks());
        print("\n");

        print("task ");
        print(name);
        print(" sleeping 100 ticks\n");

        task_sleep_ticks(100);
    }

    print("\n");
    print_color("[sleepy task]\n", COLOR_YELLOW_ON_BLACK);
    print("task ");
    print(name);
    print(" done at tick=");
    print_dec64(pit_get_ticks());
    print("\n");
}

static void cmd_tasks(const char* args) {
    (void)args;

    task_print_all();
}

static void cmd_taskdemo(const char* args) {
    (void)args;

    u32 a = task_create("demoA", demo_task, "A");
    u32 b = task_create("demoB", demo_task, "B");

    print("created demo tasks: ");
    print_dec64(a);
    print(", ");
    print_dec64(b);
    print("\n");
}

static void cmd_sleepdemo(const char* args) {
    (void)args;

    u32 a = task_create("sleepA", sleepy_task, "A");
    u32 b = task_create("sleepB", sleepy_task, "B");

    print("created sleepy tasks: ");
    print_dec64(a);
    print(", ");
    print_dec64(b);
    print("\n");
}

static void cmd_yield(const char* args) {
    (void)args;

    task_yield();
}

static void cmd_taskstats(const char* args) {
    (void)args;

    print("task count = ");
    print_dec64(task_count());
    print("\n");

    print("ready tasks = ");
    print_dec64(task_ready_count());
    print("\n");

    print("sleeping tasks = ");
    print_dec64(task_sleeping_count());
    print("\n");

    print("waiting tasks = ");
    print_dec64(task_waiting_count());
    print("\n");
}

void task_register_builtin_commands(void) {
    command_register("tasks", "list kernel tasks", cmd_tasks);
    command_register("taskdemo", "create demo cooperative tasks", cmd_taskdemo);
    command_register("sleepdemo", "create sleeping demo tasks", cmd_sleepdemo);
    command_register("yield", "yield current task", cmd_yield);
    command_register("taskstats", "show task scheduler stats", cmd_taskstats);
}
