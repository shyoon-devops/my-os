#ifndef GO_OS_TASK_H
#define GO_OS_TASK_H

#include "types.h"

typedef void (*task_entry_t)(void* arg);

typedef enum {
    TASK_STATE_UNUSED = 0,
    TASK_STATE_READY,
    TASK_STATE_RUNNING,
    TASK_STATE_SLEEPING,
    TASK_STATE_DONE
} task_state_t;

void task_init(void);

u32 task_create(const char* name, task_entry_t entry, void* arg);

u32 task_run_once(void);

void task_yield(void);
void task_sleep_ticks(u64 ticks);
void task_exit(void);

u32 task_count(void);
u32 task_ready_count(void);
u32 task_sleeping_count(void);

void task_print_all(void);
void task_register_builtin_commands(void);

const char* task_state_name(task_state_t state);

#endif
