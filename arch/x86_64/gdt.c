#include "command.h"
#include "console.h"
#include "gdt.h"
#include "print.h"
#include "types.h"

#define GDT_ENTRY_COUNT 7
#define GDT_KERNEL_STACK_SIZE 16384

typedef struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  granularity;
    u8  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct gdt_tss_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_mid1;
    u8  access;
    u8  granularity;
    u8  base_mid2;
    u32 base_high;
    u32 reserved;
} __attribute__((packed)) gdt_tss_entry_t;

typedef struct gdt_descriptor {
    u16 limit;
    u64 base;
} __attribute__((packed)) gdt_descriptor_t;

typedef struct tss64 {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist1;
    u64 ist2;
    u64 ist3;
    u64 ist4;
    u64 ist5;
    u64 ist6;
    u64 ist7;
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} __attribute__((packed)) tss64_t;

typedef union gdt_slot {
    gdt_entry_t entry;
    gdt_tss_entry_t tss;
} __attribute__((packed)) gdt_slot_t;

extern void gdt_load(gdt_descriptor_t* descriptor);
extern void tss_load(u16 selector);

static gdt_slot_t gdt[GDT_ENTRY_COUNT];
static gdt_descriptor_t gdt_descriptor;
static tss64_t tss;

static u8 kernel_stack[GDT_KERNEL_STACK_SIZE] __attribute__((aligned(16)));

static void mem_zero(void* ptr, u64 size) {
    u8* p = (u8*)ptr;

    for (u64 i = 0; i < size; i++) {
        p[i] = 0;
    }
}

static void gdt_set_entry(
    u32 index,
    u32 base,
    u32 limit,
    u8 access,
    u8 flags
) {
    gdt[index].entry.limit_low = (u16)(limit & 0xFFFF);
    gdt[index].entry.base_low = (u16)(base & 0xFFFF);
    gdt[index].entry.base_mid = (u8)((base >> 16) & 0xFF);
    gdt[index].entry.access = access;
    gdt[index].entry.granularity =
        (u8)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    gdt[index].entry.base_high = (u8)((base >> 24) & 0xFF);
}

static void gdt_set_tss(u32 index, u64 base, u32 limit) {
    gdt[index].tss.limit_low = (u16)(limit & 0xFFFF);
    gdt[index].tss.base_low = (u16)(base & 0xFFFF);
    gdt[index].tss.base_mid1 = (u8)((base >> 16) & 0xFF);
    gdt[index].tss.access = 0x89;
    gdt[index].tss.granularity = (u8)((limit >> 16) & 0x0F);
    gdt[index].tss.base_mid2 = (u8)((base >> 24) & 0xFF);
    gdt[index].tss.base_high = (u32)((base >> 32) & 0xFFFFFFFFu);
    gdt[index].tss.reserved = 0;
}

void gdt_init(void) {
    mem_zero(gdt, sizeof(gdt));
    mem_zero(&tss, sizeof(tss));

    /*
     * GDT layout:
     *
     *   0x00 null
     *   0x08 kernel code
     *   0x10 kernel data
     *   0x18 reserved, SYSRET STAR base
     *   0x20 user data
     *   0x28 user code
     *   0x30 TSS, uses two 8-byte slots internally
     */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC0);
    gdt_set_entry(3, 0, 0,      0x00, 0x00);
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xC0);
    gdt_set_entry(5, 0, 0xFFFFF, 0xFA, 0xA0);

    tss.rsp0 = gdt_kernel_stack_top();
    tss.iomap_base = sizeof(tss64_t);

    gdt_set_tss(6, (u64)&tss, sizeof(tss64_t) - 1);

    gdt_descriptor.limit = sizeof(gdt) - 1;
    gdt_descriptor.base = (u64)&gdt;

    gdt_load(&gdt_descriptor);
    tss_load(GDT_TSS_SELECTOR);

    print_color("GDT/TSS initialized\n", COLOR_GREEN_ON_BLACK);
}

u64 gdt_kernel_stack_top(void) {
    return (u64)&kernel_stack[GDT_KERNEL_STACK_SIZE];
}

u64 gdt_tss_rsp0(void) {
    return tss.rsp0;
}

u64 gdt_base_address(void) {
    return gdt_descriptor.base;
}

u16 gdt_limit(void) {
    return gdt_descriptor.limit;
}

static void cmd_gdtinfo(const char* args) {
    (void)args;

    print("GDT base = ");
    print_hex64(gdt_descriptor.base);
    print("\n");

    print("GDT limit = ");
    print_dec64(gdt_descriptor.limit);
    print("\n");

    print("kernel CS = ");
    print_hex64(GDT_KERNEL_CODE_SELECTOR);
    print("\n");

    print("kernel DS = ");
    print_hex64(GDT_KERNEL_DATA_SELECTOR);
    print("\n");

    print("user CS = ");
    print_hex64(GDT_USER_CODE_SELECTOR);
    print("\n");

    print("user DS = ");
    print_hex64(GDT_USER_DATA_SELECTOR);
    print("\n");

    print("TSS selector = ");
    print_hex64(GDT_TSS_SELECTOR);
    print("\n");

    print("TSS rsp0 = ");
    print_hex64(tss.rsp0);
    print("\n");

    print("kernel stack top = ");
    print_hex64(gdt_kernel_stack_top());
    print("\n");
}

void gdt_register_builtin_commands(void) {
    command_register("gdtinfo", "show GDT/TSS state", cmd_gdtinfo);
}
