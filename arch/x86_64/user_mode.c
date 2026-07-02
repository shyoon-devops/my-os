#include "command.h"
#include "console.h"
#include "gdt.h"
#include "print.h"
#include "types.h"
#include "user_mode.h"

#define PAGE_SIZE 4096
#define USER_STACK_SIZE 16384

#define PTE_PRESENT 0x001ull
#define PTE_RW      0x002ull
#define PTE_USER    0x004ull
#define PTE_PS      0x080ull
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ull

extern u64 ring3_enter(u64 user_rip, u64 user_rsp);
extern void ring3_user_entry(void);

extern u8 ring3_user_blob_start[];
extern u8 ring3_user_blob_end[];

static u8 ring3_stack[USER_STACK_SIZE] __attribute__((aligned(PAGE_SIZE)));

static u32 ready = 0;

static inline u64 read_cr3(void) {
    u64 value;

    __asm__ volatile (
        "mov %%cr3, %0"
        : "=r"(value)
    );

    return value;
}

static u64 align_down(u64 value, u64 align) {
    return value & ~(align - 1);
}

static u64 align_up(u64 value, u64 align) {
    return (value + align - 1) & ~(align - 1);
}

static u64* table_from_entry(u64 entry) {
    return (u64*)(entry & PTE_ADDR_MASK);
}

static void mark_user_page(u64 vaddr) {
    u64* pml4 = (u64*)(read_cr3() & PTE_ADDR_MASK);

    u64 pml4_i = (vaddr >> 39) & 0x1FF;
    u64 pdpt_i = (vaddr >> 30) & 0x1FF;
    u64 pd_i   = (vaddr >> 21) & 0x1FF;
    u64 pt_i   = (vaddr >> 12) & 0x1FF;

    if ((pml4[pml4_i] & PTE_PRESENT) == 0) {
        return;
    }

    pml4[pml4_i] |= PTE_USER | PTE_RW;

    u64* pdpt = table_from_entry(pml4[pml4_i]);

    if ((pdpt[pdpt_i] & PTE_PRESENT) == 0) {
        return;
    }

    pdpt[pdpt_i] |= PTE_USER | PTE_RW;

    if (pdpt[pdpt_i] & PTE_PS) {
        return;
    }

    u64* pd = table_from_entry(pdpt[pdpt_i]);

    if ((pd[pd_i] & PTE_PRESENT) == 0) {
        return;
    }

    pd[pd_i] |= PTE_USER | PTE_RW;

    if (pd[pd_i] & PTE_PS) {
        return;
    }

    u64* pt = table_from_entry(pd[pd_i]);

    if ((pt[pt_i] & PTE_PRESENT) == 0) {
        return;
    }

    pt[pt_i] |= PTE_USER | PTE_RW;
}

static void mark_user_range(u64 start, u64 size) {
    if (size == 0) {
        return;
    }

    u64 begin = align_down(start, PAGE_SIZE);
    u64 end = align_up(start + size, PAGE_SIZE);

    for (u64 addr = begin; addr < end; addr += PAGE_SIZE) {
        mark_user_page(addr);
    }

    __asm__ volatile ("mov %%cr3, %%rax\nmov %%rax, %%cr3" ::: "rax", "memory");
}

void user_mode_mark_user_range(u64 start, u64 size) {
    mark_user_range(start, size);
}

u64 user_mode_enter(u64 user_rip) {
    if (!ready) {
        return 0xFFFFFFFFFFFFFFFFULL;
    }

    return ring3_enter(user_rip, user_mode_stack_top());
}

void user_mode_init(void) {
    u64 blob_start = (u64)ring3_user_blob_start;
    u64 blob_end = (u64)ring3_user_blob_end;

    mark_user_range(blob_start, blob_end - blob_start);
    mark_user_range((u64)ring3_stack, USER_STACK_SIZE);

    ready = 1;

    print_color("Ring3 user mode smoke test prepared\n", COLOR_GREEN_ON_BLACK);
}

u32 user_mode_ready(void) {
    return ready;
}

u64 user_mode_stack_bottom(void) {
    return (u64)&ring3_stack[0];
}

u64 user_mode_stack_top(void) {
    return (u64)&ring3_stack[USER_STACK_SIZE];
}

u64 user_mode_entry_address(void) {
    return (u64)ring3_user_entry;
}

u64 user_mode_blob_start(void) {
    return (u64)ring3_user_blob_start;
}

u64 user_mode_blob_end(void) {
    return (u64)ring3_user_blob_end;
}

static void cmd_ring3info(const char* args) {
    (void)args;

    print("ring3 ready = ");
    print_dec64(ready);
    print("\n");

    print("entry = ");
    print_hex64(user_mode_entry_address());
    print("\n");

    print("blob start = ");
    print_hex64(user_mode_blob_start());
    print("\n");

    print("blob end = ");
    print_hex64(user_mode_blob_end());
    print("\n");

    print("stack bottom = ");
    print_hex64(user_mode_stack_bottom());
    print("\n");

    print("stack top = ");
    print_hex64(user_mode_stack_top());
    print("\n");

    print("user CS = ");
    print_hex64(GDT_USER_CODE_SELECTOR);
    print("\n");

    print("user DS = ");
    print_hex64(GDT_USER_DATA_SELECTOR);
    print("\n");

    print("kernel rsp0 = ");
    print_hex64(gdt_tss_rsp0());
    print("\n");
}

static void cmd_ring3test(const char* args) {
    (void)args;

    if (!ready) {
        print("ring3 is not ready\n");
        return;
    }

    print("entering ring3...\n");

    u64 exit_code = ring3_enter(
        user_mode_entry_address(),
        user_mode_stack_top()
    );

    print("returned from ring3\n");
    print("exit code = ");
    print_dec64(exit_code);
    print("\n");
}

void user_mode_register_builtin_commands(void) {
    command_register("ring3info", "show ring3 smoke test state", cmd_ring3info);
    command_register("ring3test", "enter ring3 and return through SYS_exit", cmd_ring3test);
}
