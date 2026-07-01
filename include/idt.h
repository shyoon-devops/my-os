#ifndef GO_OS_IDT_H
#define GO_OS_IDT_H

#include "types.h"

void idt_init(void);

void interrupts_enable(void);
void interrupts_disable(void);

void idt_test_page_fault(void);

void exception_handler(
    u64 vector,
    u64 error_code,
    u64 rip,
    u64 cs,
    u64 rflags,
    u64 cr2
);

/*
 * interrupt.asm의 irq_common에서 호출하는 C entry.
 */
void irq_handler(u64 vector);

#endif