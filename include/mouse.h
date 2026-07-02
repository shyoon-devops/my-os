#ifndef MY_OS_MOUSE_H
#define MY_OS_MOUSE_H

#include "types.h"

void mouse_init(void);
void mouse_on_irq(void);

u32 mouse_packet_count(void);
u32 mouse_wheel_event_count(void);
s32 mouse_wheel_position(void);

void mouse_register_builtin_commands(void);

#endif
