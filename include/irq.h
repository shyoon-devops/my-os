#ifndef GO_OS_IRQ_H
#define GO_OS_IRQ_H

#include "types.h"

#define IRQ_COUNT 16

typedef void (*irq_handler_t)(void);

void irq_init(void);
u32 irq_register_handler(u8 irq, irq_handler_t handler);
void irq_unregister_handler(u8 irq);
void irq_dispatch(u8 irq);

#endif