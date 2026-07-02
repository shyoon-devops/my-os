#include "command.h"
#include "console.h"
#include "device.h"
#include "fd.h"
#include "heap.h"
#include "idt.h"
#include "irq.h"
#include "keyboard.h"
#include "klog.h"
#include "multiboot2.h"
#include "panic.h"
#include "pic.h"
#include "pit.h"
#include "pmm.h"
#include "print.h"
#include "ramfs.h"
#include "rtc.h"
#include "serial.h"
#include "shell.h"
#include "task.h"
#include "timer.h"
#include "tty.h"
#include "types.h"
#include "vfs.h"
#include "vmm.h"
#include "workqueue.h"

#ifndef MB2_BOOTLOADER_MAGIC
#define MB2_BOOTLOADER_MAGIC 0x36D76289u
#endif

static void kernel_halt(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void kernel_main(u32 magic, u64 mb2_info_addr) {
    console_clear();

    serial_init();

    klog_init();

    print_color("Hello from x86_64 kernel\n", COLOR_GREEN_ON_BLACK);
    print("Loaded by GRUB Multiboot2\n");
    print("Long mode enabled\n");
    print("\n");

    print("multiboot2 magic = ");
    print_hex32(magic);
    print("\n");

    print("multiboot2 info  = ");
    print_hex64(mb2_info_addr);
    print("\n\n");

    if (magic != MB2_BOOTLOADER_MAGIC) {
        print_color("Multiboot2 magic INVALID\n", COLOR_RED_ON_BLACK);
        print("\nKernel halted.");
        kernel_halt();
    }

    print_color("Multiboot2 magic OK\n", COLOR_GREEN_ON_BLACK);

    print("\n");
    idt_init();

    print("\n");
    irq_init();

    print("\n");
    mb2_dump_info(mb2_info_addr);

    print("\n");
    pmm_init(mb2_info_addr);

    print("\n");
    heap_init();

    print("\n");
    device_init();

    device_register(
        "console0",
        DEVICE_TYPE_DISPLAY,
        DEVICE_STATE_READY,
        "vga-text"
    );

    serial_register_device();

    print("\n");
    workqueue_init();

    print("\n");
    timer_init();

    print("\n");
    rtc_init();

    print("\n");
    task_init();

    print("\n");
    tty_init();

    print("\n");
    ramfs_init();

    print("\n");
    fd_init();

    print("\n");
    command_init();
    command_register_builtin_commands();
    device_register_builtin_commands();
    klog_register_builtin_commands();
    panic_register_builtin_commands();
    workqueue_register_builtin_commands();
    timer_register_builtin_commands();
    rtc_register_builtin_commands();
    task_register_builtin_commands();
    vfs_register_builtin_commands();
    fd_register_builtin_commands();

    print("\n");
    pic_init();

    print("\n");
    pit_init(100);

    print("\n");
    keyboard_init();

    print("\n");
    interrupts_enable();
    print_color("Interrupts enabled\n", COLOR_GREEN_ON_BLACK);

    print("\n");
    shell_init();
    shell_start_task();

    for (;;) {
        timer_poll();
        workqueue_run_pending(16);
        task_run_once();

        __asm__ volatile ("hlt");
    }
}
