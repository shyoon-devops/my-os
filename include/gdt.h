#ifndef MY_OS_GDT_H
#define MY_OS_GDT_H

#include "types.h"

#define GDT_KERNEL_CODE_SELECTOR 0x08
#define GDT_KERNEL_DATA_SELECTOR 0x10

/*
 * STAR[63:48] = 0x18 로 두면 SYSRET 기준:
 *   user SS = 0x18 + 8  | 3 = 0x23
 *   user CS = 0x18 + 16 | 3 = 0x2B
 */
#define GDT_USER_DATA_SELECTOR   0x23
#define GDT_USER_CODE_SELECTOR   0x2B

#define GDT_TSS_SELECTOR         0x30

void gdt_init(void);

u64 gdt_kernel_stack_top(void);
u64 gdt_tss_rsp0(void);
u64 gdt_base_address(void);
u16 gdt_limit(void);

void gdt_register_builtin_commands(void);

#endif
