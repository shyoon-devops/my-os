#include "console.h"
#include "idt.h"
#include "irq.h"
#include "print.h"
#include "types.h"

#define IDT_ENTRIES 256
#define KERNEL_CODE_SELECTOR 0x08
#define IDT_TYPE_INTERRUPT_GATE 0x8E

typedef struct __attribute__((packed)) {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 zero;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    u16 limit;
    u64 base;
} idt_pointer_t;

static idt_entry_t idt[IDT_ENTRIES];

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

static void kernel_halt(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void idt_set_gate(u32 vector, u64 handler, u16 selector, u8 type_attr) {
    idt[vector].offset_low = (u16)(handler & 0xFFFF);
    idt[vector].selector = selector;
    idt[vector].ist = 0;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_mid = (u16)((handler >> 16) & 0xFFFF);
    idt[vector].offset_high = (u32)((handler >> 32) & 0xFFFFFFFF);
    idt[vector].zero = 0;
}

static void lidt(idt_pointer_t* idtr) {
    __asm__ volatile (
        "lidt (%0)"
        :
        : "r"(idtr)
        : "memory"
    );
}

static const char* exception_name(u64 vector) {
    switch (vector) {
        case 0: return "#DE Divide Error";
        case 1: return "#DB Debug";
        case 2: return "NMI Interrupt";
        case 3: return "#BP Breakpoint";
        case 4: return "#OF Overflow";
        case 5: return "#BR Bound Range Exceeded";
        case 6: return "#UD Invalid Opcode";
        case 7: return "#NM Device Not Available";
        case 8: return "#DF Double Fault";
        case 9: return "Coprocessor Segment Overrun";
        case 10: return "#TS Invalid TSS";
        case 11: return "#NP Segment Not Present";
        case 12: return "#SS Stack-Segment Fault";
        case 13: return "#GP General Protection Fault";
        case 14: return "#PF Page Fault";
        case 16: return "#MF x87 Floating-Point Exception";
        case 17: return "#AC Alignment Check";
        case 18: return "#MC Machine Check";
        case 19: return "#XM SIMD Floating-Point Exception";
        case 20: return "#VE Virtualization Exception";
        case 21: return "#CP Control Protection Exception";
        case 29: return "#VC VMM Communication Exception";
        case 30: return "#SX Security Exception";
        default: return "Unknown Exception";
    }
}

static void print_page_fault_error(u64 error_code) {
    print("  present    = ");
    print_dec64(error_code & 1ULL);
    print("\n");

    print("  write      = ");
    print_dec64((error_code >> 1) & 1ULL);
    print("\n");

    print("  user       = ");
    print_dec64((error_code >> 2) & 1ULL);
    print("\n");

    print("  reserved   = ");
    print_dec64((error_code >> 3) & 1ULL);
    print("\n");

    print("  instr_fetch= ");
    print_dec64((error_code >> 4) & 1ULL);
    print("\n");
}

static void idt_register_exceptions(void) {
    idt_set_gate(0,  (u64)isr0,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(1,  (u64)isr1,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(2,  (u64)isr2,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(3,  (u64)isr3,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(4,  (u64)isr4,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(5,  (u64)isr5,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(6,  (u64)isr6,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(7,  (u64)isr7,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(8,  (u64)isr8,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(9,  (u64)isr9,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(10, (u64)isr10, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(11, (u64)isr11, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(12, (u64)isr12, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(13, (u64)isr13, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(14, (u64)isr14, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(15, (u64)isr15, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(16, (u64)isr16, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(17, (u64)isr17, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(18, (u64)isr18, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(19, (u64)isr19, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(20, (u64)isr20, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(21, (u64)isr21, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(22, (u64)isr22, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(23, (u64)isr23, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(24, (u64)isr24, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(25, (u64)isr25, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(26, (u64)isr26, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(27, (u64)isr27, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(28, (u64)isr28, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(29, (u64)isr29, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(30, (u64)isr30, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(31, (u64)isr31, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
}

static void idt_register_irqs(void) {
    idt_set_gate(32, (u64)irq0,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(33, (u64)irq1,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(34, (u64)irq2,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(35, (u64)irq3,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(36, (u64)irq4,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(37, (u64)irq5,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(38, (u64)irq6,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(39, (u64)irq7,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(40, (u64)irq8,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(41, (u64)irq9,  KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(42, (u64)irq10, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(43, (u64)irq11, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(44, (u64)irq12, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(45, (u64)irq13, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(46, (u64)irq14, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(47, (u64)irq15, KERNEL_CODE_SELECTOR, IDT_TYPE_INTERRUPT_GATE);
}

void idt_init(void) {
    for (u32 i = 0; i < IDT_ENTRIES; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attr = 0;
        idt[i].offset_mid = 0;
        idt[i].offset_high = 0;
        idt[i].zero = 0;
    }

    idt_register_exceptions();
    idt_register_irqs();

    idt_pointer_t idtr;
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (u64)&idt[0];

    lidt(&idtr);

    print_color("IDT loaded\n", COLOR_GREEN_ON_BLACK);
}

void interrupts_enable(void) {
    __asm__ volatile ("sti");
}

void interrupts_disable(void) {
    __asm__ volatile ("cli");
}

void exception_handler(
    u64 vector,
    u64 error_code,
    u64 rip,
    u64 cs,
    u64 rflags,
    u64 cr2
) {
    print_color("\n[CPU Exception]\n", COLOR_RED_ON_BLACK);

    print("vector = ");
    print_dec64(vector);
    print(" ");
    print(exception_name(vector));
    print("\n");

    print("error  = ");
    print_hex64(error_code);
    print("\n");

    print("rip    = ");
    print_hex64(rip);
    print("\n");

    print("cs     = ");
    print_hex64(cs);
    print("\n");

    print("rflags = ");
    print_hex64(rflags);
    print("\n");

    if (vector == 14) {
        print("cr2    = ");
        print_hex64(cr2);
        print("\n");

        print_page_fault_error(error_code);
    }

    print_color("kernel halted by exception\n", COLOR_RED_ON_BLACK);
    kernel_halt();
}

void irq_handler(u64 vector) {
    if (vector < 32 || vector > 47) {
        return;
    }

    u8 irq = (u8)(vector - 32);

    irq_dispatch(irq);
}

void idt_test_page_fault(void) {
    print_color("[IDT page fault test]\n", COLOR_YELLOW_ON_BLACK);
    print("about to read unmapped address 0x0000000050000000\n");

    volatile u64* p = (volatile u64*)0x0000000050000000ULL;
    volatile u64 value = *p;

    (void)value;

    print_color("page fault test unexpectedly returned\n", COLOR_RED_ON_BLACK);
}