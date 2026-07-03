# my-os 마일스톤

x86_64 toy OS 학습 프로젝트의 로드맵. 기준은 "명령 하나 돌아가게 하기"가 아니라 최종 목표까지의 흐름이다.

## 최종 목표

QEMU 안의 userland shell에서:

```text
my-os$ llm "hello"
```

를 실행하면 OS 내부 네트워크 스택과 HTTPS 클라이언트를 통해 LLM API까지 호출된다.

```text
GRUB
  → my-os kernel
    → /bin/init
      → /bin/sh
        → llm "hello"
          → DNS → TCP → TLS → HTTPS POST → LLM API → streaming response
```

## 개발 원칙

```text
증상 패치 금지. 먼저 실행 모델을 본다.
```

문제를 볼 때 항상 이 순서로 본다:

1. 이 코드는 어느 task context에서 도는가?
2. 이 stack은 누가 소유하는가?
3. blocking/wakeup은 어떤 task를 재우는가?
4. syscall return은 어느 kernel stack으로 돌아가는가?
5. shell task와 user program 책임이 섞였는가?
6. per-process 상태가 필요한데 전역 변수로 때우고 있지 않은가?

실행 경계가 깨진 문제면 해당 파일 패치가 아니라 task/process 구조부터 바로잡는다.

한 phase = 검증 가능한 작은 단위. 진행 루프는 고정이다:

```text
1. Claude: 현재 단계만 구현 + 설명 + 로컬 커밋 (앞서나가기 금지)
2. 사용자: 직접 실행·검수·학습
3. 사용자가 다음 단계 요청 → Claude가 github/gitea에 push
4. 반복
```

이 프로젝트의 목적은 사용자가 OS 개발 과정을 단계별로 직접 보고 느끼는 것이다.

---

## ✅ M0. 완료된 기반 (phase-01 ~ 10k)

상세 검증 명령은 각 셸 builtin 참고 (`help`).

- [x] **Boot / Long mode**: GRUB Multiboot2 → 32-bit entry → long mode → 64-bit kernel, VGA/serial 출력
- [x] **출력/디버깅 기반**: console, serial, print/print_hex64/print_dec64, panic, klog ring buffer
- [x] **Memory 1차**: Multiboot2 memory map 파싱, PMM(비트맵), VMM(4-level 페이징), heap (`pmmtest`/`vmmtest`/`heaptest`)
- [x] **CPU 구조**: GDT/TSS(rsp0), IDT, exception handler, PIC remap, IRQ dispatch (`gdtinfo`/`syscallcpuinfo`)
- [x] **Timer/입력**: PIT(100Hz), keyboard/mouse IRQ, TTY input buffer, 특수키 처리
- [x] **Cooperative scheduler**: task_create/yield/sleep/exit, READY~DONE 상태 (`tasks`/`taskstats`/`taskdemo`/`sleepdemo`)
- [x] **Wait queue**: blocking input — shell task가 입력 없을 때 busy loop 대신 WAITING
- [x] **VFS/RAMFS/initramfs**: tar 로딩, `/dev/tty0` 디바이스 노드 (`ls /`, `cat /etc/motd`, `initramfsinfo`)
- [x] **FD layer**: fd 0/1/2 → `/dev/tty0`, SYS_read/write → fd_read/fd_write
- [x] **Syscall CPU path**: SYSCALL entry(수동 kernel stack 전환), dispatch table — read(0)/write(1)/getpid(39)/exit(60)
- [x] **Ring3 smoke test**: `ring3test` — ring3 진입/복귀 자체 검증
- [x] **User ELF loader**: ELF64 검증, PT_LOAD 매핑(단일 LOAD + RWX로 단순화), user stack 매핑, entry 진입
- [x] **Exec command + completion**: `exec /bin/init|hello|readkey`, 명령·경로 자동완성
- [x] **User ELF 별도 task 실행** (phase-10k): shell task가 직접 ring3 진입하던 잘못된 구조를 user-elf task 분리로 교정, blocking stdin read 복원

> 교훈: SYSCALL은 RSP를 kernel stack으로 자동 전환하지 않는다. 그리고 user program이 shell task 안에서 돌면 blocking/exit 복귀가 전부 꼬인다.

## 🔥 M1. user task/process 경계 안정화 — 현재 (phase-10l)

가장 의심해야 할 다음 구조 문제: **`ring3_saved_kernel_rsp`가 전역 하나다** (`arch/x86_64/ring3_switch.asm`). user task가 하나일 때만 간신히 맞는 구조.

- [ ] `ring3_saved_kernel_rsp` 전역 제거 또는 최소화 — per-task/per-exec context로 이동
  - `ring3_enter_ex(user_rip, user_rsp, saved_rsp_slot)` 또는 `current_task()->user_context.saved_kernel_rsp` 조회
- [ ] user_exec_ctx 정리: saved_kernel_rsp / user_rsp / user_rip / exit_code / status
- [ ] SYS_exit 복귀 안정화
- [ ] blocking read가 정확히 user task만 재우는지 재검증

**완료 기준**: 여러 user task가 순차 실행되어도 SYS_exit 복귀가 깨지지 않음.

## 📋 M2. Process struct 도입

task(커널 스케줄링 단위)와 process(user 프로그램 실행 단위)를 분리한다. 초기에는 task 1개 = process 1개.

- [ ] `process_t`: pid, address space, file table, cwd, exit code
- [ ] API: process_create(path) / process_exit(code) / process_wait(pid) / process_current()
- [ ] exit status table, parent/child 관계, process state

**완료 기준**: `ps`, `wait`, `exec /bin/hello`에서 process 상태 확인.

## 📋 M3. Per-process address space

현재는 모든 user program이 kernel page table 안의 같은 range를 공유 → 동시 실행 불가, isolation 없음.

- [ ] process별 page table, context switch 시 CR3 전환
- [ ] kernel mapping 공유 + user lower-half per-process 매핑 (user mapping 소유권을 process 단위로)
- [ ] process 종료 시 매핑/프레임 회수 일원화

**완료 기준**: 두 user process가 같은 0x50000000에 로드되어도 충돌하지 않음. user page fault 시 kernel panic이 아니라 해당 process kill.

## 📋 M4. Userland runtime: crt0 + argv/envp

- [ ] `user/crt0`: `_start` 공통화 → `main(argc, argv)` 호출 → return 값을 SYS_exit으로
- [ ] exec 인자 파싱, user stack에 argc/argv layout 구성 (strings → NULL → pointers → argc, alignment 유지)

**완료 기준**:

```text
exec /bin/args hello world
argc = 3
argv[0] = /bin/args
argv[1] = hello
argv[2] = world
```

## 📋 M5. Userland 기본 프로그램

kernel shell 명령을 userland로 빼낸다.

- [ ] syscall 확장: open/close/stat/readdir (+ 기존 read/write/getpid/exit)
- [ ] `/bin/echo`, `/bin/cat`, `/bin/ls`, `/bin/args`

**완료 기준**: `exec /bin/echo hello`, `exec /bin/cat /etc/motd`, `exec /bin/ls /bin`

## 📋 M6. User shell

prompt를 kernel shell에서 userland로 이관한다.

- [ ] `/bin/sh`: prompt, read line, parse, exec, wait
- [ ] builtins: cd, exit, pwd
- [ ] 부팅 체인: kernel → `/bin/init` → `/bin/sh`

**완료 기준**:

```text
my-os$ echo hello
my-os$ cat /etc/motd
```

## 📋 M7. File system 확장

- [ ] open/read/write/close, stat, readdir, cwd, path resolution 정리
- [ ] (후순위) tarfs/simplefs, block device, virtio-blk — LLM 목표에 필수 아니므로 뒤로 밀 수 있음

## 📋 M8. Network 1차: NIC driver

QEMU에서 다루기 쉬운 NIC부터. 후보: virtio-net 또는 e1000.

- [ ] NIC init, MAC 읽기, TX/RX packet, interrupt 또는 polling

**완료 기준**: `netinfo`, raw ethernet frame 송신, broadcast frame 수신.

## 📋 M9. Ethernet / ARP / IPv4 / ICMP

- [ ] Ethernet frame parser → ARP request/reply → IPv4 parser + checksum → ICMP echo

**완료 기준**: `net ping 10.0.2.2` (QEMU gateway)에 준하는 ICMP echo 확인.

## 📋 M10. UDP / DNS

TCP보다 UDP/DNS가 먼저다.

- [ ] UDP socket-ish layer, DNS A record query/parsing

**완료 기준**: `dns example.com` → `example.com -> <ip>`

## 📋 M11. TCP (client 최소 구현)

초기 범위: client only, single connection, congestion control 최소화, blocking API, IPv4 only.

- [ ] 3-way handshake, send/receive/ack, 최소 retransmit, close

**완료 기준**: `tcp connect <ip> 80` → HTTP request 송신 → response 수신.

## 📋 M12. HTTP client

- [ ] GET request (Host header), Content-Length, chunked 최소 처리

**완료 기준**: `httpget http://example.com/` → `HTTP/1.1 200 OK` + HTML 일부 출력.

## 📋 M13. TLS / HTTPS

단계적으로 간다:

- [ ] Phase A: TLS handshake 성공 (검증 없이)
- [ ] Phase B: 인증서 chain 파싱 (X.509)
- [ ] Phase C: root CA 검증
- [ ] Phase D: HTTPS request/response

필요 요소: random/entropy, time, SHA256, HMAC, AES-GCM 또는 ChaCha20-Poly1305, ECDHE. 직접 최소 구현 vs 작은 TLS 라이브러리 포팅은 그때 결정.

**완료 기준**: `httpsget https://example.com/`

## 📋 M14. JSON / SSE streaming parser

- [ ] JSON stringify + parser (초기: request 생성 + response에서 output text만 추출)
- [ ] SSE streaming parser

## 📋 M15. LLM API client

OpenAI-compatible endpoint 호출. 설정은 `/etc/llm.conf` (base_url/model/api_key).

- [ ] `llm "..."` → JSON request → HTTPS POST → response text 출력

**완료 기준**: `my-os$ llm "hello"` → 응답 출력.

## 🏁 M16. 최종: Userland LLM shell

- [ ] `/bin/sh`에서 `llm` 실행, streaming 응답 표시

```text
my-os$ llm "쿠버네티스 파드가 ImagePullBackOff일 때 점검 순서 알려줘"
1. image name/tag 확인
...
```

## ♻️ M17. 안정화 / 테스트 (지속)

기능이 붙을 때마다 같이 늘린다.

- 테스트: boot / memory / scheduler / wait queue / syscall / ELF loader / user process / network packet / HTTP parser / TLS handshake / LLM request
- 디버깅 명령: tasks, taskstats, syscallinfo, ring3info, elfinfo, proc, ps, vmmap, netinfo, tcpinfo

---

## 현재 위치 한 줄 요약

```text
"ELF가 실행된다" 단계에서 "user process 모델을 제대로 세우는" 단계로 넘어가는 중.
작은 증상 수정이 아니라 process / task / stack / syscall / address space의 소유권을 먼저 맞춘다.
```
