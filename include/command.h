#ifndef MY_OS_COMMAND_H
#define MY_OS_COMMAND_H

#include "types.h"

typedef void (*command_handler_t)(const char* args);

void command_init(void);

u32 command_register(
    const char* name,
    const char* description,
    command_handler_t handler
);

u32 command_execute(const char* line);

void command_print_help(void);
void command_register_builtin_commands(void);

#endif