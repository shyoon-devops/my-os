ASM=nasm
CC=gcc
LD=ld
QEMU=qemu-system-x86_64

BUILD_DIR=build
ISO_DIR=$(BUILD_DIR)/iso

KERNEL=$(BUILD_DIR)/kernel.elf
ISO=$(BUILD_DIR)/my-os.iso

GIT_MSG?=

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
    kernel/wait.c \
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
    kernel/vfs.c \
    kernel/ramfs.c \
    kernel/fd.c \
    kernel/utils.c

ASM_SOURCES= \
    kernel/interrupt.asm \
    kernel/task_switch.asm

BOOT_OBJECT=$(BUILD_DIR)/boot.o
C_OBJECTS=$(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ASM_OBJECTS=$(patsubst %.asm,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

QEMU_MEM?=256M

QEMU_COMMON=-cdrom $(ISO) \
            -m $(QEMU_MEM) \
            -no-reboot \
            -no-shutdown

all: $(ISO)

$(BOOT_OBJECT): boot.asm
	mkdir -p $(@D)
	$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.asm
	mkdir -p $(@D)
	$(ASM) -f elf64 $< -o $@

$(KERNEL): $(BOOT_OBJECT) $(C_OBJECTS) $(ASM_OBJECTS) linker.ld
	mkdir -p $(@D)
	$(LD) $(LDFLAGS) -o $(KERNEL) $(BOOT_OBJECT) $(C_OBJECTS) $(ASM_OBJECTS)

$(ISO): $(KERNEL) grub.cfg
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/kernel.elf
	cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR)

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

rename:
	./scripts/rename-my-os.sh

git-status:
	git status --short

commit:
	@if [ -z "$(GIT_MSG)" ]; then \
	  echo 'Usage: make commit GIT_MSG="[phase-04] your message"'; \
	  exit 1; \
	fi
	git add --all
	git commit -m "$(GIT_MSG)"
	git push

clean:
	rm -rf $(BUILD_DIR)
	rm -rf iso
	rm -f boot.o kernel/*.o kernel.elf os.iso my-os.iso serial.log

.PHONY: all check run run-curses run-cocoa run-cocoa-full run-cocoa-serial run-curses-serial-log rename git-status commit clean
