#ifndef GO_OS_VMM_H
#define GO_OS_VMM_H

#include "types.h"

#define PTE_PRESENT  0x001ULL
#define PTE_WRITABLE 0x002ULL
#define PTE_USER     0x004ULL
#define PTE_HUGE     0x080ULL

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

#define VMM_TEST_VIRT        0x0000000040000000ULL
#define VMM_UNMAP_TEST_VIRT  0x0000000040001000ULL

void vmm_map_page(u64 virt_addr, u64 phys_addr, u64 flags);
u64 vmm_unmap_page(u64 virt_addr, u32 free_phys);
u64 vmm_get_mapping(u64 virt_addr);
void vmm_test_mapping(void);
void vmm_test_unmap(void);

#endif
