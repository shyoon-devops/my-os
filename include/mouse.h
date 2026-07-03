#ifndef MY_OS_MOUSE_H
#define MY_OS_MOUSE_H

#include "types.h"

void mouse_init(void);
void mouse_on_irq(void);

void mouse_register_builtin_commands(void);

#endif
