#include "command.h"
#include "console.h"
#include "msr.h"
#include "print.h"
#include "syscall.h"
#include "syscall_cpu.h"
#include "types.h"

#define RFLAGS_IF 0x00000200ull

/*
 * 현재 boot GDT 기준으로 kernel code selector는 보통 0x08이다.
 *
 * syscall/sysret에서 user selector는 STAR 인코딩이 조금 특이하다.
 * Long mode SYSRET의 user CS는 STAR[63:48] + 16 + RPL3 형태로 계산된다.
 *
 * 그래서 Phase 10에서 GDT를 다음처럼 맞출 예정이다.
 *
 *   0x08 kernel code
 *   0x10 kernel data
 *   0x20 user data
 *   0x28 user code
 *
 * SYSRET 결과:
 *   user CS = 0x28 | 3 = 0x2B
 *   user SS = 0x20 | 3 = 0x23
 *
 * 이를 위해 STAR[63:48]에는 0x18을 넣는다.
 */
#define SYSCALL_KERNEL_CS          0x08u
#define SYSCALL_USER_STAR_CS_BASE  0x18u

extern void syscall_entry(void);

static u32 cpu_syscall_enabled = 0;

static u64 saved_efer_before = 0;
static u64 saved_efer_after = 0;
static u64 saved_star = 0;
static u64 saved_lstar = 0;
static u64 saved_fmask = 0;

static void print_bool(u32 value) {
    print(value ? "yes" : "no");
}

void syscall_cpu_init(void) {
    saved_efer_before = rdmsr(MSR_IA32_EFER);

    saved_lstar = (u64)syscall_entry;

    saved_star =
        ((u64)SYSCALL_USER_STAR_CS_BASE << 48) |
        ((u64)SYSCALL_KERNEL_CS << 32);

    /*
     * syscall 진입 시 IF를 끈 상태로 들어오게 한다.
     * 실제 interrupt 정책은 Phase 10 이후 kernel stack 전환과 함께 정리한다.
     */
    saved_fmask = RFLAGS_IF;

    wrmsr(MSR_IA32_STAR, saved_star);
    wrmsr(MSR_IA32_LSTAR, saved_lstar);
    wrmsr(MSR_IA32_FMASK, saved_fmask);

    wrmsr(MSR_IA32_EFER, saved_efer_before | IA32_EFER_SCE);

    saved_efer_after = rdmsr(MSR_IA32_EFER);

    cpu_syscall_enabled =
        (saved_efer_after & IA32_EFER_SCE) ? 1 : 0;

    print_color("Syscall CPU entry configured\n", COLOR_GREEN_ON_BLACK);
}

u32 syscall_cpu_enabled(void) {
    return cpu_syscall_enabled;
}

u64 syscall_cpu_entry_address(void) {
    return (u64)syscall_entry;
}

u64 syscall_cpu_efer_before(void) {
    return saved_efer_before;
}

u64 syscall_cpu_efer_after(void) {
    return saved_efer_after;
}

u64 syscall_cpu_star(void) {
    return saved_star;
}

u64 syscall_cpu_lstar(void) {
    return saved_lstar;
}

u64 syscall_cpu_fmask(void) {
    return saved_fmask;
}

static void cmd_syscallcpuinfo(const char* args) {
    (void)args;

    print("syscall CPU enabled = ");
    print_bool(cpu_syscall_enabled);
    print("\n");

    print("syscall_entry = ");
    print_hex64((u64)syscall_entry);
    print("\n");

    print("IA32_EFER before = ");
    print_hex64(saved_efer_before);
    print("\n");

    print("IA32_EFER after  = ");
    print_hex64(saved_efer_after);
    print("\n");

    print("IA32_STAR  = ");
    print_hex64(saved_star);
    print("\n");

    print("IA32_LSTAR = ");
    print_hex64(saved_lstar);
    print("\n");

    print("IA32_FMASK = ");
    print_hex64(saved_fmask);
    print("\n");
}

static void cmd_syscallentrytest(const char* args) {
    (void)args;

    /*
     * 아직 ring3에서 syscall instruction을 실행하지 않는다.
     * 대신 CPU entry가 가리키는 최종 dispatcher와 같은 syscall_dispatch()
     * 경로를 한 번 더 검증한다.
     */
    const char* message = "hello through syscall CPU prep path\n";

    u64 written = syscall_dispatch(
        SYS_WRITE,
        1,
        (u64)message,
        36,
        0,
        0,
        0
    );

    print("dispatch returned ");
    print_dec64(written);
    print("\n");

    print("actual syscall instruction test is deferred to Phase 10\n");
}

void syscall_cpu_register_builtin_commands(void) {
    command_register(
        "syscallcpuinfo",
        "show syscall CPU MSR configuration",
        cmd_syscallcpuinfo
    );

    command_register(
        "syscallentrytest",
        "test syscall CPU prep path without ring3",
        cmd_syscallentrytest
    );
}
