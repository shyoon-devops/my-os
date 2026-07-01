#ifndef GO_OS_PIT_H
#define GO_OS_PIT_H

#include "types.h"

void pit_init(u32 frequency);
void pit_on_tick(void);
u64 pit_get_ticks(void);
void pit_wait_ticks(u64 ticks);
void pit_test(void);

#endif