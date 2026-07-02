#ifndef MY_OS_TIMER_H
#define MY_OS_TIMER_H

#include "types.h"
#include "workqueue.h"

#define TIMER_MAX_ITEMS 64

void timer_init(void);

u32 timer_schedule_ticks(u64 delay_ticks, work_func_t func, void* arg);
u32 timer_poll(void);

u32 timer_pending_count(void);
u64 timer_fired_count(void);
u64 timer_failed_count(void);

void timer_register_builtin_commands(void);

#endif
