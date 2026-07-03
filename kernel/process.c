#include "command.h"
#include "console.h"
#include "heap.h"
#include "print.h"
#include "process.h"
#include "spinlock.h"
#include "task.h"
#include "types.h"
#include "user_mode.h"

#define PROCESS_NAME_MAX 128

typedef struct process {
    u32 pid;
    u32 parent_pid;
    char name[PROCESS_NAME_MAX];

    process_state_t state;
    u64 exit_code;
    s32 load_status; /* 0 = 정상, -1 = ELF 로드/실행 실패 */

    u32 task_id; /* 이 process를 실행하는 user-elf task */

    struct process* next;
} process_t;

static spinlock_t process_lock;

static process_t* process_head = 0;
static process_t* process_tail = 0;

static u32 next_pid = 1;
static u32 total_process_count = 0;

static void process_copy_name(process_t* proc, const char* path) {
    u64 i = 0;

    if (path) {
        while (path[i] && i + 1 < PROCESS_NAME_MAX) {
            proc->name[i] = path[i];
            i++;
        }
    }

    proc->name[i] = '\0';
}

static process_t* process_find_by_pid_locked(u32 pid) {
    process_t* current = process_head;

    while (current) {
        if (current->pid == pid) {
            return current;
        }

        current = current->next;
    }

    return 0;
}

static process_t* process_find_by_task_locked(u32 task_id) {
    if (task_id == 0) {
        return 0;
    }

    process_t* current = process_head;

    while (current) {
        if (current->task_id == task_id &&
            current->state == PROCESS_STATE_RUNNING) {
            return current;
        }

        current = current->next;
    }

    return 0;
}

static void process_finish(process_t* proc, s32 load_status, u64 exit_code) {
    if (!proc) {
        return;
    }

    u64 flags;
    spin_lock_irqsave(&process_lock, &flags);

    proc->load_status = load_status;
    proc->exit_code = exit_code;
    proc->state = PROCESS_STATE_ZOMBIE;

    spin_unlock_irqrestore(&process_lock, flags);
}

/*
 * user-elf task의 entry.
 * 이 task context 안에서 ELF 로드와 ring3 진입이 모두 일어난다.
 */
static void process_task_entry(void* arg) {
    process_t* proc = (process_t*)arg;

    if (!proc) {
        return;
    }

    u64 exit_code = 0;
    s32 status = user_mode_run_path(proc->name, &exit_code);

    process_finish(proc, status, exit_code);
}

void process_init(void) {
    spinlock_init(&process_lock);

    process_head = 0;
    process_tail = 0;

    next_pid = 1;
    total_process_count = 0;

    print_color("Process table initialized\n", COLOR_GREEN_ON_BLACK);
}

u32 process_create(const char* path) {
    if (!path || path[0] == '\0') {
        return 0;
    }

    process_t* proc = (process_t*)kzalloc(sizeof(process_t));

    if (!proc) {
        print_color("process_create failed: no memory\n", COLOR_RED_ON_BLACK);
        return 0;
    }

    process_copy_name(proc, path);

    proc->parent_pid = process_current_pid();
    proc->state = PROCESS_STATE_RUNNING;
    proc->exit_code = 0;
    proc->load_status = 0;

    /*
     * pid를 먼저 확정하고 task를 만든다.
     * cooperative scheduler라서 task_create 직후에 task가
     * 바로 실행되는 일은 없다 (현재 task가 yield해야 돈다).
     */
    u64 flags;
    spin_lock_irqsave(&process_lock, &flags);

    proc->pid = next_pid;
    next_pid++;

    if (!process_head) {
        process_head = proc;
        process_tail = proc;
    } else {
        process_tail->next = proc;
        process_tail = proc;
    }

    total_process_count++;

    spin_unlock_irqrestore(&process_lock, flags);

    u32 task_id = task_create("user-elf", process_task_entry, proc);

    if (task_id == 0) {
        process_finish(proc, -1, 0);
        return 0;
    }

    proc->task_id = task_id;

    return proc->pid;
}

u32 process_current_pid(void) {
    u32 task_id = task_current_id();

    if (task_id == 0) {
        return 0;
    }

    u64 flags;
    spin_lock_irqsave(&process_lock, &flags);

    process_t* proc = process_find_by_task_locked(task_id);
    u32 pid = proc ? proc->pid : 0;

    spin_unlock_irqrestore(&process_lock, flags);

    return pid;
}

void process_exit(u64 exit_code) {
    u32 task_id = task_current_id();

    if (task_id == 0) {
        return;
    }

    u64 flags;
    spin_lock_irqsave(&process_lock, &flags);

    process_t* proc = process_find_by_task_locked(task_id);

    spin_unlock_irqrestore(&process_lock, flags);

    process_finish(proc, 0, exit_code);
}

s32 process_wait(u32 pid, u64* out_exit_code) {
    if (out_exit_code) {
        *out_exit_code = 0;
    }

    for (;;) {
        u64 flags;
        spin_lock_irqsave(&process_lock, &flags);

        process_t* proc = process_find_by_pid_locked(pid);

        if (!proc) {
            spin_unlock_irqrestore(&process_lock, flags);
            return PROCESS_WAIT_NO_PROCESS;
        }

        if (proc->state == PROCESS_STATE_ZOMBIE ||
            proc->state == PROCESS_STATE_REAPED) {
            proc->state = PROCESS_STATE_REAPED;

            s32 load_status = proc->load_status;
            u64 exit_code = proc->exit_code;

            spin_unlock_irqrestore(&process_lock, flags);

            if (out_exit_code) {
                *out_exit_code = exit_code;
            }

            return (load_status == 0)
                ? PROCESS_WAIT_OK
                : PROCESS_WAIT_LOAD_FAILED;
        }

        spin_unlock_irqrestore(&process_lock, flags);

        /*
         * 아직 RUNNING이면 다른 task(= user-elf task)에게 CPU를 넘긴다.
         * shell task 안이면 yield, scheduler context면 run_once.
         */
        if (task_current()) {
            task_yield();
        } else {
            task_run_once();
        }
    }
}

const char* process_state_name(process_state_t state) {
    switch (state) {
        case PROCESS_STATE_RUNNING:
            return "running";
        case PROCESS_STATE_ZOMBIE:
            return "zombie";
        case PROCESS_STATE_REAPED:
            return "reaped";
        case PROCESS_STATE_UNUSED:
            return "unused";
        default:
            return "unknown";
    }
}

static void cmd_ps(const char* args) {
    (void)args;

    u64 flags;
    spin_lock_irqsave(&process_lock, &flags);

    print("processes:\n");

    process_t* current = process_head;

    while (current) {
        print("  pid=");
        print_dec64(current->pid);

        print(" ppid=");
        print_dec64(current->parent_pid);

        print(" task=");
        print_dec64(current->task_id);

        print(" state=");
        print(process_state_name(current->state));

        if (current->state == PROCESS_STATE_ZOMBIE ||
            current->state == PROCESS_STATE_REAPED) {
            if (current->load_status != 0) {
                print(" load=failed");
            } else {
                print(" exit=");
                print_dec64(current->exit_code);
            }
        }

        print(" ");
        print(current->name);
        print("\n");

        current = current->next;
    }

    print("process count = ");
    print_dec64(total_process_count);
    print("\n");

    spin_unlock_irqrestore(&process_lock, flags);
}

static u32 parse_pid(const char* args, u32* out_pid) {
    if (!args || !out_pid) {
        return 0;
    }

    while (*args == ' ' || *args == '\t') {
        args++;
    }

    if (*args < '0' || *args > '9') {
        return 0;
    }

    u32 value = 0;

    while (*args >= '0' && *args <= '9') {
        value = value * 10 + (u32)(*args - '0');
        args++;
    }

    *out_pid = value;

    return 1;
}

static void cmd_wait(const char* args) {
    u32 pid = 0;

    if (!parse_pid(args, &pid)) {
        print("usage: wait <pid>\n");
        return;
    }

    u64 exit_code = 0;
    s32 result = process_wait(pid, &exit_code);

    if (result == PROCESS_WAIT_NO_PROCESS) {
        print("wait: no such pid ");
        print_dec64(pid);
        print("\n");
        return;
    }

    if (result == PROCESS_WAIT_LOAD_FAILED) {
        print("wait: pid ");
        print_dec64(pid);
        print(" failed to load\n");
        return;
    }

    print("wait: pid ");
    print_dec64(pid);
    print(" exit code = ");
    print_dec64(exit_code);
    print("\n");
}

void process_register_builtin_commands(void) {
    command_register("ps", "list processes", cmd_ps);
    command_register("wait", "wait a pid and read its exit code", cmd_wait);
}
