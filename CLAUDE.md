# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A freestanding **x86_64 kernel written in C**, booted by GRUB via Multiboot2. This directory (`temp/os-lab05`) is the active lab and is self-contained (its own `Makefile`, `boot.asm`, `linker.ld`). It is untracked in git; the tracked project at the repo root (`../../`) is an older single-file experiment (`kernel.go`, `boot.asm`) plus the documentation set. Do not confuse the two — work here happens against the C kernel under `kernel/` and `include/`.

The learning docs (concepts + phase devlogs) live at the repo root under `../../docs/`, not in this directory.

## Build & run

The full toolchain (`nasm`, `grub-mkrescue`/`xorriso`, cross `ld`) is **not installed on the macOS host** — only `gcc`, `ld`, and `qemu` are. Building the ISO must happen inside the Docker container; QEMU can run on the host against the produced `os.iso`.

```bash
# One-time: build the toolchain image (from repo root, ../../)
docker build --platform linux/amd64 -t go-os-build ..

# Enter the container (mounts this dir at /work)
docker run --rm -it --platform linux/amd64 -v "$PWD:/work" go-os-build

# Inside the container:
make            # boot.o + kernel/*.o -> kernel.elf -> os.iso (grub-mkrescue)
make check      # verify Multiboot2 header + ELF class/machine/entry + sections
make clean

# On the macOS host, run the freshly built ISO:
make run-cocoa          # cocoa display, zoom-to-fit (preferred)
make run-cocoa-serial   # + serial to stdio
make run-curses-serial-log  # curses + serial.log (for capturing boot output)
```

`QEMU_MEM` (default `256M`) is overridable: `make run-cocoa QEMU_MEM=512M`.

There is no automated test suite. Verification is manual: boot in QEMU and exercise the shell, or capture `serial.log` via `make run-curses-serial-log`.

## Boot flow

1. `boot.asm` (`bits 32`) — GRUB enters at `_start`. Checks the Multiboot2 magic in `eax`, saves `eax`/`ebx`, sets a temp stack.
2. `setup_page_tables` identity-maps the first 1 GiB using 512 × 2 MiB pages (PML4 → PDPT → PD in `.bss`).
3. Enables PAE (`CR4`), long mode (`EFER.LME`), then paging + protection (`CR0`); loads the 64-bit GDT; far-jumps into `bits 64` `long_mode_start`.
4. Passes args per System V ABI (`edi` = magic, `esi` = mb2 info) and `call kernel_main`.

## Kernel architecture

`kernel/kernel.c :: kernel_main()` is the single init sequence, and **order is load-bearing**: IDT → IRQ → PMM → heap → devices → workqueue → timer → rtc → task → tty → shell/commands → PIC → PIT → keyboard → `interrupts_enable()`. Set up handlers and controllers before unmasking interrupts. After init it enters the main loop: `timer_poll()` → `workqueue_run_pending()` → `task_run_once()` → `hlt`.

Each subsystem is a `kernel/<name>.c` + `include/<name>.h` pair (one small file per concern):

- **Boot info / memory**: `multiboot2` (tag + memory-map parse), `pmm` (bitmap physical frame allocator seeded from the mmap), `vmm` (page map/unmap), `heap` (kernel allocator).
- **Interrupts**: `idt`, `irq`, `pic` (8259), plus `interrupt.asm` (ISR/IRQ stubs). `pit` (100 Hz) and `keyboard` are IRQ sources.
- **Devices / IO**: `device` (registry), `console` (VGA text + scroll), `print` (formatting on top of console), `serial` (COM), `tty`, `rtc`.
- **Concurrency**: `task` (cooperative scheduler, with `task_switch.asm`), `workqueue`, `spinlock`, `timer`.
- **Shell**: `shell` + `command` (command registry). Subsystems self-register shell commands via `*_register_builtin_commands()` calls in `kernel_main` — follow this pattern when adding a command-exposing subsystem.
- **Diagnostics**: `klog`, `panic`.

### Adding a source file

Add the `.c` to `C_SOURCES` (or the `.asm` to `ASM_SOURCES`) in the `Makefile`, create the matching `include/*.h`, and wire its `_init()` into `kernel_main` at the correct point in the ordering above.

## Constraints when writing kernel code

This is freestanding: no libc, no `float`/SSE/MMX (`-mno-sse -mno-mmx`), no red zone, no stack protector, no PIC/PIE. Use the fixed-width types in `include/types.h` (`u8`/`u32`/`u64`, etc.) and the helpers in `utils.h`/`io.h` (port I/O) rather than reaching for standard headers. Code is compiled `-Wall -Wextra -O2` — keep it warning-clean.

## Documentation conventions

Concept and devlog docs at `../../docs/` are written in Korean and follow strict style rules in `../../docs/00-CONVENTIONS.md` (golden sample: `docs/concepts-boot.md`) and `../../docs/CONVENTIONS_VISUAL.md` (registers/memory as address+value tables, flows as mermaid, warnings as callouts). After finishing a phase, save a QEMU screenshot under `docs/assets/` and write a first-person devlog under `docs/devlog/phaseN.md`.
