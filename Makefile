ASM=nasm
CC=gcc
LD=ld
QEMU=qemu-system-x86_64

CFLAGS=-std=gnu11 \
       -ffreestanding \
       -fno-builtin \
       -fno-stack-protector \
       -fno-pic \
       -fno-pie \
       -fno-asynchronous-unwind-tables \
       -fno-unwind-tables \
       -m64 \
       -mno-red-zone \
       -mno-mmx \
       -mno-sse \
       -mno-sse2 \
       -Wall \
       -Wextra \
       -O2 \
       -Iinclude

LDFLAGS=-T linker.ld \
        -nostdlib \
        -z max-page-size=0x1000 \
        -m elf_x86_64

KERNEL=kernel.elf
ISO=os.iso

C_SOURCES= \
    kernel/kernel.c \
    kernel/console.c \
    kernel/print.c \
    kernel/multiboot2.c \
    kernel/pmm.c \
    kernel/vmm.c \
    kernel/idt.c \
    kernel/irq.c \
    kernel/pic.c \
    kernel/pit.c \
    kernel/keyboard.c \
    kernel/tty.c \
    kernel/shell.c \
    kernel/heap.c \
    kernel/command.c \
    kernel/device.c \
    kernel/serial.c \
    kernel/klog.c \
    kernel/panic.c \
    kernel/spinlock.c \
    kernel/workqueue.c \
    kernel/timer.c \
    kernel/rtc.c \
    kernel/task.c \
    kernel/utils.c

C_OBJECTS=$(C_SOURCES:.c=.o)

ASM_SOURCES= \
    kernel/interrupt.asm \
    kernel/task_switch.asm

ASM_OBJECTS=$(ASM_SOURCES:.asm=.o)

QEMU_MEM?=256M

QEMU_COMMON=-cdrom $(ISO) \
            -m $(QEMU_MEM) \
            -no-reboot \
            -no-shutdown

all: $(ISO)

boot.o: boot.asm
	$(ASM) -f elf64 boot.asm -o boot.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/%.o: kernel/%.asm
	$(ASM) -f elf64 $< -o $@

$(KERNEL): boot.o $(C_OBJECTS) $(ASM_OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -o $(KERNEL) boot.o $(C_OBJECTS) $(ASM_OBJECTS)

$(ISO): $(KERNEL) grub.cfg
	mkdir -p iso/boot/grub
	cp $(KERNEL) iso/boot/kernel.elf
	cp grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) iso

check: $(KERNEL)
	grub-file --is-x86-multiboot2 $(KERNEL)
	@echo "Multiboot2 check OK"
	@readelf -h $(KERNEL) | grep -E 'Class|Machine|Entry'
	@readelf -S $(KERNEL) | grep -E 'multiboot|text|rodata|data|bss'

run: $(ISO)
	$(QEMU) $(QEMU_COMMON)

run-curses: $(ISO)
	$(QEMU) $(QEMU_COMMON) \
	  -display curses

run-cocoa: $(ISO)
	$(QEMU) $(QEMU_COMMON) \
	  -display cocoa,zoom-to-fit=on

run-cocoa-full: $(ISO)
	$(QEMU) $(QEMU_COMMON) \
	  -display cocoa,zoom-to-fit=on \
	  -full-screen

run-cocoa-serial: $(ISO)
	$(QEMU) $(QEMU_COMMON) \
	  -display cocoa,zoom-to-fit=on \
	  -serial stdio

run-curses-serial-log: $(ISO)
	rm -f serial.log
	$(QEMU) $(QEMU_COMMON) \
	  -display curses \
	  -serial file:serial.log

clean:
	rm -rf boot.o kernel/*.o $(KERNEL) $(ISO) iso serial.log
