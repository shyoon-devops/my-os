# my-os

GRUB Multiboot2로 부팅하고 32비트 entry에서 x86_64 long mode로 전환한 뒤, C 커널과 ring3 userland까지 올리는 OS 학습 프로젝트입니다.

전체 로드맵과 진행 상태는 [MILESTONES.md](MILESTONES.md)를 참고하세요.

## 구조

```text
.
├── Makefile
├── linker.ld            # kernel 링커 스크립트
├── grub.cfg
├── arch/x86_64/         # 어셈블리 + CPU 종속 코드
│   ├── boot.asm         # Multiboot2 entry, long mode 전환
│   ├── gdt.c            # GDT/TSS
│   ├── interrupt.asm    # ISR/IRQ 스텁
│   ├── task_switch.asm  # task context switch
│   ├── syscall_entry.asm# SYSCALL entry (kernel stack 전환)
│   ├── ring3_switch.asm # iretq ring3 진입/복귀
│   └── user_mode.c      # exec 명령, user ELF를 별도 task로 실행
├── kernel/              # 아키텍처 독립 커널
│   ├── kernel.c         # 초기화 순서 + main loop
│   ├── pmm.c vmm.c heap.c
│   ├── idt.c irq.c pic.c pit.c timer.c rtc.c
│   ├── keyboard.c mouse.c tty.c console.c serial.c klog.c
│   ├── task.c wait.c workqueue.c spinlock.c panic.c
│   ├── vfs.c ramfs.c initramfs.c device.c fd.c
│   ├── elf.c syscall.c syscall_cpu.c
│   └── shell.c command.c print.c utils.c
├── include/             # kernel 헤더
├── user/                # userland 프로그램 (freestanding C)
│   ├── init.c hello.c readkey.c
│   ├── syscall.h        # userland syscall 래퍼
│   └── linker.ld        # 0x50000000, 단일 LOAD 세그먼트
├── initramfs/           # initramfs 소스 트리 (tar로 커널에 링크됨)
└── scripts/
    ├── build-run.sh     # Docker 빌드 + QEMU 실행 원스톱
    ├── docker-build-os.sh
    └── docker-shell.sh
```

## 빌드 / 실행

빌드는 Docker(linux/amd64), 실행은 호스트 QEMU에서 합니다. 호스트에서 직접 `make` 하지 마세요 (macOS 툴체인으로는 elf64 커널을 만들 수 없습니다).

```bash
# 원스톱: Docker 빌드 + QEMU 실행
bash scripts/build-run.sh --clean --run-cocoa

# 빌드만
./scripts/docker-build-os.sh          # 증분
./scripts/docker-build-os.sh clean    # 클린

# 실행만 (ISO 빌드돼 있어야 함)
make run-cocoa          # macOS 창
make run-curses         # 터미널
make run-cocoa-serial   # serial → stdio
```

Docker 이미지는 처음 빌드 시 자동 생성됩니다 (`docker-build-os.sh`의 ensure_image).

## 현재 기능 (phase-10k)

- GRUB Multiboot2 부팅, x86_64 long mode
- VGA text console (스크롤백), serial, klog
- PMM(비트맵) / VMM(4-level 페이징) / kernel heap
- GDT/TSS, IDT, PIC/PIT, 키보드/마우스
- 협력적 task 스케줄러 + wait queue 기반 blocking
- VFS + ramfs + initramfs(tar) + `/dev/tty0` + fd 테이블
- ELF64 로더, SYSCALL 경로 (read/write/getpid/exit)
- 대화형 셸: 히스토리, tab 완성, `help`로 명령 목록 확인
- userland: `/bin/init`, `/bin/hello`, `/bin/readkey` — `exec /bin/<name>`으로 별도 task에서 ring3 실행

## QEMU에서 확인해볼 것

```text
my-os> help
my-os> ls /bin
my-os> cat /etc/motd
my-os> tasks
my-os> ring3test
my-os> exec /bin/readkey
```
