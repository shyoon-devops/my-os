ASM=nasm
CC=gcc
LD=ld
QEMU=qemu-system-x86_64

BUILD_DIR=build
ISO_DIR=$(BUILD_DIR)/iso
INITRAMFS_DIR=initramfs
USER_DIR=user
ARCH_DIR=arch/x86_64

KERNEL=$(BUILD_DIR)/kernel.elf
ISO=$(BUILD_DIR)/my-os.iso

INITRAMFS_TAR=$(BUILD_DIR)/initramfs.tar
INITRAMFS_OBJECT=$(BUILD_DIR)/initramfs.o
INITRAMFS_STAMP=$(BUILD_DIR)/.initramfs-prepared

USER_PROGRAMS=init hello readkey
USER_ELFS=$(addprefix $(INITRAMFS_DIR)/bin/,$(USER_PROGRAMS))

INITRAMFS_FILES=$(shell find $(INITRAMFS_DIR) -type f 2>/dev/null)

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

USER_CFLAGS=-std=gnu11 \
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
            -I$(USER_DIR)

USER_LDFLAGS=-nostdlib \
             -static \
             -no-pie \
             -Wl,-T,$(USER_DIR)/linker.ld \
             -Wl,--build-id=none

LDFLAGS=-T linker.ld \
        -nostdlib \
        -z noexecstack \
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
    kernel/mouse.c \
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
    kernel/initramfs.c \
    kernel/elf.c \
    kernel/syscall.c \
    kernel/syscall_cpu.c \
    kernel/fd.c \
    kernel/utils.c

ARCH_C_SOURCES= \
    $(ARCH_DIR)/gdt.c \
    $(ARCH_DIR)/user_mode.c

ASM_SOURCES= \
    $(ARCH_DIR)/interrupt.asm \
    $(ARCH_DIR)/task_switch.asm \
    $(ARCH_DIR)/syscall_entry.asm \
    $(ARCH_DIR)/gdt_load.asm \
    $(ARCH_DIR)/ring3_switch.asm

BOOT_SOURCE=$(ARCH_DIR)/boot.asm
BOOT_OBJECT=$(BUILD_DIR)/$(ARCH_DIR)/boot.o

C_OBJECTS=$(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ARCH_C_OBJECTS=$(patsubst %.c,$(BUILD_DIR)/%.o,$(ARCH_C_SOURCES))
ASM_OBJECTS=$(patsubst %.asm,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

QEMU_MEM?=256M

QEMU_COMMON=-cdrom $(ISO) \
            -m $(QEMU_MEM) \
            -no-reboot \
            -no-shutdown

all: $(ISO)

$(INITRAMFS_STAMP):
	mkdir -p $(INITRAMFS_DIR)/bin
	mkdir -p $(INITRAMFS_DIR)/etc
	@if [ ! -f $(INITRAMFS_DIR)/hello.txt ]; then \
	  printf 'hello from my-os initramfs\n' > $(INITRAMFS_DIR)/hello.txt; \
	fi
	@if [ ! -f $(INITRAMFS_DIR)/etc/motd ]; then \
	  printf 'welcome to my-os\nthis file was loaded from initramfs\n' > $(INITRAMFS_DIR)/etc/motd; \
	fi
	@if [ ! -f $(INITRAMFS_DIR)/bin/README ]; then \
	  printf 'This directory will later contain userland ELF binaries.\nFor now it is loaded from initramfs.\n' > $(INITRAMFS_DIR)/bin/README; \
	fi
	touch $(INITRAMFS_STAMP)

$(INITRAMFS_DIR)/bin/%: $(USER_DIR)/%.c $(USER_DIR)/syscall.h $(USER_DIR)/linker.ld | $(INITRAMFS_STAMP)
	mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ $<

$(INITRAMFS_TAR): $(USER_ELFS) $(INITRAMFS_FILES) | $(INITRAMFS_STAMP)
	mkdir -p $(@D)
	tar --format=ustar -cf $(INITRAMFS_TAR) -C $(INITRAMFS_DIR) .

$(INITRAMFS_OBJECT): $(INITRAMFS_TAR)
	mkdir -p $(@D)
	$(LD) -r -b binary -o $(INITRAMFS_OBJECT) $(INITRAMFS_TAR)

$(BOOT_OBJECT): $(BOOT_SOURCE)
	mkdir -p $(@D)
	$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.asm
	mkdir -p $(@D)
	$(ASM) -f elf64 $< -o $@

$(KERNEL): $(BOOT_OBJECT) $(C_OBJECTS) $(ARCH_C_OBJECTS) $(ASM_OBJECTS) $(INITRAMFS_OBJECT) linker.ld
	mkdir -p $(@D)
	$(LD) $(LDFLAGS) -o $(KERNEL) $(BOOT_OBJECT) $(C_OBJECTS) $(ARCH_C_OBJECTS) $(ASM_OBJECTS) $(INITRAMFS_OBJECT)

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

require-iso:
	@if [ ! -f "$(ISO)" ]; then \
	  echo "Missing $(ISO)"; \
	  echo "Build it first with:"; \
	  echo "  ./scripts/docker-build-os.sh"; \
	  exit 1; \
	fi

run: require-iso
	$(QEMU) $(QEMU_COMMON)

run-curses: require-iso
	$(QEMU) $(QEMU_COMMON) \
	  -display curses

run-cocoa: require-iso
	$(QEMU) $(QEMU_COMMON) \
	  -display cocoa,zoom-to-fit=on

run-cocoa-full: require-iso
	$(QEMU) $(QEMU_COMMON) \
	  -display cocoa,zoom-to-fit=on \
	  -full-screen

run-cocoa-serial: require-iso
	$(QEMU) $(QEMU_COMMON) \
	  -display cocoa,zoom-to-fit=on \
	  -serial stdio

run-curses-serial-log: require-iso
	rm -f serial.log
	$(QEMU) $(QEMU_COMMON) \
	  -display curses \
	  -serial file:serial.log

rename:
	./scripts/rename-my-os.sh

git-status:
	git status --short

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(USER_ELFS)
	rm -rf iso
	rm -f boot.o kernel/*.o kernel.elf os.iso my-os.iso serial.log

.PHONY: all check require-iso run run-curses run-cocoa run-cocoa-full run-cocoa-serial run-curses-serial-log rename git-status commit clean

.PHONY: commit push-remotes

commit:
	@if [ -z "$(GIT_MSG)" ]; then \
		echo "Usage: make commit GIT_MSG=\"commit message\""; \
		exit 1; \
	fi
	@git add -A
	@if git diff --cached --quiet; then \
		echo "nothing to commit"; \
	else \
		git commit -m "$(GIT_MSG)"; \
	fi

push-remotes:
	@branch="$$(git branch --show-current)"; \
	if [ -z "$$branch" ]; then \
		echo "detached HEAD: cannot push current branch"; \
		exit 1; \
	fi; \
	remotes="$$(git remote)"; \
	if [ -z "$$remotes" ]; then \
		echo "no git remotes configured"; \
		exit 1; \
	fi; \
	for remote in $$remotes; do \
		echo "pushing $$branch to $$remote"; \
		git push "$$remote" "$$branch"; \
	done
