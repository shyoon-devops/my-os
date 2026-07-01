#include "console.h"
#include "irq.h"
#include "pic.h"
#include "print.h"
#include "types.h"

static irq_handler_t irq_handlers[IRQ_COUNT];

void irq_init(void) {
    for (u8 i = 0; i < IRQ_COUNT; i++) {
        irq_handlers[i] = 0;
    }

    print_color("IRQ dispatch table initialized\n", COLOR_GREEN_ON_BLACK);
}

u32 irq_register_handler(u8 irq, irq_handler_t handler) {
    if (irq >= IRQ_COUNT) {
        return 0;
    }

    if (!handler) {
        return 0;
    }

    irq_handlers[irq] = handler;

    return 1;
}

void irq_unregister_handler(u8 irq) {
    if (irq >= IRQ_COUNT) {
        return;
    }

    irq_handlers[irq] = 0;
}

void irq_dispatch(u8 irq) {
    if (irq >= IRQ_COUNT) {
        return;
    }

    if (irq_handlers[irq]) {
        irq_handlers[irq]();
    }

    /*
     * PIC에게 이 IRQ 처리가 끝났다고 알려준다.
     */
    pic_send_eoi(irq);
}