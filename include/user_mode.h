#ifndef MY_OS_USER_MODE_H
#define MY_OS_USER_MODE_H

#include "types.h"

void user_mode_init(void);

u32 user_mode_ready(void);
u64 user_mode_stack_bottom(void);
u64 user_mode_stack_top(void);
u64 user_mode_entry_address(void);
u64 user_mode_blob_start(void);
u64 user_mode_blob_end(void);

void user_mode_register_builtin_commands(void);

#endif
