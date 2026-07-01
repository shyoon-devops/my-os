# go-os Multiboot2 x86_64 split version

GRUB Multiboot2로 부팅하고, 32비트 entry에서 x86_64 long mode로 전환한 뒤 C 커널을 실행하는 실습 프로젝트입니다.

## 구조

```text
.
├── Dockerfile
├── Makefile
├── boot.asm
├── linker.ld
├── grub.cfg
├── include/
│   ├── types.h
│   ├── console.h
│   ├── print.h
│   ├── multiboot2.h
│   ├── pmm.h
│   ├── vmm.h
│   └── utils.h
└── kernel/
    ├── kernel.c
    ├── console.c
    ├── print.c
    ├── multiboot2.c
    ├── pmm.c
    ├── vmm.c
    └── utils.c
```

## Docker 빌드

```bash
docker build --platform linux/amd64 -t go-os-build .
```

```bash
docker run --rm -it \
  --platform linux/amd64 \
  -v "$PWD:/work" \
  go-os-build
```

컨테이너 안에서:

```bash
make clean
make
make check
make run-curses
```

macOS 호스트 QEMU로 실행:

```bash
make clean
make
make check
make run-cocoa
```

전체화면:

```bash
make run-cocoa-full
```

## 현재 기능

- GRUB Multiboot2 부팅
- x86_64 long mode 전환
- VGA text console + scroll
- Multiboot2 tag / memory map 파싱
- bitmap 기반 PMM physical frame allocator
- VMM page map / unmap 테스트
