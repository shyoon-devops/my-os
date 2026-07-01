#include "console.h"
#include "device.h"
#include "io.h"
#include "irq.h"
#include "pic.h"
#include "pit.h"
#include "print.h"
#include "types.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_FREQUENCY 1193182u

static volatile u64 pit_ticks = 0;
static device_t* pit_device = 0;

void pit_init(u32 frequency) {
    if (frequency == 0) {
        frequency = 100;
    }

    u32 divisor = PIT_BASE_FREQUENCY / frequency;

    if (divisor == 0) {
        divisor = 1;
    }

    if (divisor > 65535) {
        divisor = 65535;
    }

    outb(PIT_COMMAND, 0x36);

    outb(PIT_CHANNEL0, (u8)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (u8)((divisor >> 8) & 0xFF));

    pit_ticks = 0;

    irq_register_handler(0, pit_on_tick);
    pic_clear_mask(0);

    pit_device = device_register(
        "pit0",
        DEVICE_TYPE_TIMER,
        DEVICE_STATE_READY,
        "pit"
    );

    print_color("PIT initialized and IRQ0 registered\n", COLOR_GREEN_ON_BLACK);

    print("PIT frequency = ");
    print_dec64(frequency);
    print(" Hz\n");

    print("PIT divisor   = ");
    print_dec64(divisor);
    print("\n");
}

void pit_on_tick(void) {
    pit_ticks++;
}

u64 pit_get_ticks(void) {
    return pit_ticks;
}

void pit_wait_ticks(u64 ticks) {
    u64 target = pit_ticks + ticks;

    while (pit_ticks < target) {
        __asm__ volatile ("hlt");
    }
}

void pit_test(void) {
    print_color("[PIT timer test]\n", COLOR_YELLOW_ON_BLACK);

    u64 before = pit_get_ticks();

    print("ticks before = ");
    print_dec64(before);
    print("\n");

    print("waiting 5 timer ticks...\n");

    pit_wait_ticks(5);

    u64 after = pit_get_ticks();

    print("ticks after  = ");
    print_dec64(after);
    print("\n");

    if (after >= before + 5) {
        print_color("PIT timer OK\n", COLOR_GREEN_ON_BLACK);
    } else {
        print_color("PIT timer FAILED\n", COLOR_RED_ON_BLACK);
    }
}