#ifndef MY_OS_WORKQUEUE_H
#define MY_OS_WORKQUEUE_H

#include "types.h"

typedef void (*work_func_t)(void* arg);

void workqueue_init(void);

u32 workqueue_schedule(work_func_t func, void* arg);
u32 workqueue_run_pending(u32 max_items);

u32 workqueue_pending_count(void);
u64 workqueue_executed_count(void);
u64 workqueue_dropped_count(void);

void workqueue_register_builtin_commands(void);

#endif
