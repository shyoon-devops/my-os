#ifndef MY_OS_VMM_H
#define MY_OS_VMM_H

#include "types.h"

static const u64 PTE_PRESENT  = 0x001ULL;
static const u64 PTE_WRITABLE = 0x002ULL;
static const u64 PTE_USER     = 0x004ULL;
static const u64 PTE_HUGE     = 0x080ULL;

static const u64 PTE_ADDR_MASK = 0x000FFFFFFFFFF000ULL;

static const u64 VMM_TEST_VIRT       = 0x0000000040000000ULL;
static const u64 VMM_UNMAP_TEST_VIRT = 0x0000000040001000ULL;

void vmm_map_page(u64 virt_addr, u64 phys_addr, u64 flags);
u64 vmm_unmap_page(u64 virt_addr, u32 free_phys);
u64 vmm_get_mapping(u64 virt_addr);
void vmm_test_mapping(void);
void vmm_test_unmap(void);

#endif
