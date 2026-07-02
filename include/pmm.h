#ifndef MY_OS_PMM_H
#define MY_OS_PMM_H

#include "types.h"

#define PAGE_SIZE 4096ULL
#define MIN_USABLE_ADDR 0x00100000ULL

void pmm_init(u64 mb2_info_addr);
u64 pmm_alloc_frame(void);
void pmm_free_frame(u64 frame_addr);
u64 pmm_free_frame_count(void);
void pmm_test(void);

#endif
