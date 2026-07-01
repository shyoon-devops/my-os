#include "console.h"
#include "pmm.h"
#include "print.h"
#include "utils.h"
#include "vmm.h"

static u64 read_cr3(void) {
    u64 value;

    __asm__ volatile (
        "mov %%cr3, %0"
        : "=r"(value)
    );

    return value;
}

static void invlpg(u64 virt_addr) {
    __asm__ volatile (
        "invlpg (%0)"
        :
        : "r"(virt_addr)
        : "memory"
    );
}

static void zero_page(u64 phys_addr) {
    u64* p = (u64*)phys_addr;

    for (u64 i = 0; i < PAGE_SIZE / sizeof(u64); i++) {
        p[i] = 0;
    }
}

static u64* vmm_get_or_create_next_table(u64* table, u64 index) {
    u64 entry = table[index];

    if (entry & PTE_PRESENT) {
        if (entry & PTE_HUGE) {
            return 0;
        }

        return (u64*)(entry & PTE_ADDR_MASK);
    }

    u64 new_table_phys = pmm_alloc_frame();

    if (new_table_phys == 0) {
        return 0;
    }

    zero_page(new_table_phys);

    table[index] = new_table_phys | PTE_PRESENT | PTE_WRITABLE;

    return (u64*)new_table_phys;
}

static u64* vmm_get_pte_ptr(u64 virt_addr) {
    virt_addr = align_down_u64(virt_addr, PAGE_SIZE);

    u64 pml4_index = (virt_addr >> 39) & 0x1FF;
    u64 pdpt_index = (virt_addr >> 30) & 0x1FF;
    u64 pd_index   = (virt_addr >> 21) & 0x1FF;
    u64 pt_index   = (virt_addr >> 12) & 0x1FF;

    u64* pml4 = (u64*)(read_cr3() & PTE_ADDR_MASK);

    u64 pml4e = pml4[pml4_index];
    if (!(pml4e & PTE_PRESENT)) {
        return 0;
    }

    if (pml4e & PTE_HUGE) {
        return 0;
    }

    u64* pdpt = (u64*)(pml4e & PTE_ADDR_MASK);

    u64 pdpte = pdpt[pdpt_index];
    if (!(pdpte & PTE_PRESENT)) {
        return 0;
    }

    if (pdpte & PTE_HUGE) {
        return 0;
    }

    u64* pd = (u64*)(pdpte & PTE_ADDR_MASK);

    u64 pde = pd[pd_index];
    if (!(pde & PTE_PRESENT)) {
        return 0;
    }

    if (pde & PTE_HUGE) {
        return 0;
    }

    u64* pt = (u64*)(pde & PTE_ADDR_MASK);

    return &pt[pt_index];
}

void vmm_map_page(u64 virt_addr, u64 phys_addr, u64 flags) {
    virt_addr = align_down_u64(virt_addr, PAGE_SIZE);
    phys_addr = align_down_u64(phys_addr, PAGE_SIZE);

    u64 pml4_index = (virt_addr >> 39) & 0x1FF;
    u64 pdpt_index = (virt_addr >> 30) & 0x1FF;
    u64 pd_index   = (virt_addr >> 21) & 0x1FF;
    u64 pt_index   = (virt_addr >> 12) & 0x1FF;

    u64* pml4 = (u64*)(read_cr3() & PTE_ADDR_MASK);

    u64* pdpt = vmm_get_or_create_next_table(pml4, pml4_index);
    if (!pdpt) {
        print_color("vmm_map_page: failed at PDPT\n", COLOR_RED_ON_BLACK);
        return;
    }

    u64* pd = vmm_get_or_create_next_table(pdpt, pdpt_index);
    if (!pd) {
        print_color("vmm_map_page: failed at PD\n", COLOR_RED_ON_BLACK);
        return;
    }

    u64* pt = vmm_get_or_create_next_table(pd, pd_index);
    if (!pt) {
        print_color("vmm_map_page: failed at PT\n", COLOR_RED_ON_BLACK);
        return;
    }

    pt[pt_index] = phys_addr | flags | PTE_PRESENT;

    invlpg(virt_addr);
}

u64 vmm_unmap_page(u64 virt_addr, u32 free_phys) {
    virt_addr = align_down_u64(virt_addr, PAGE_SIZE);

    u64* pte = vmm_get_pte_ptr(virt_addr);

    if (!pte) {
        return 0;
    }

    if (!(*pte & PTE_PRESENT)) {
        return 0;
    }

    u64 phys_addr = *pte & PTE_ADDR_MASK;

    *pte = 0;

    invlpg(virt_addr);

    if (free_phys && phys_addr != 0) {
        pmm_free_frame(phys_addr);
    }

    return phys_addr;
}

u64 vmm_get_mapping(u64 virt_addr) {
    virt_addr = align_down_u64(virt_addr, PAGE_SIZE);

    u64 pml4_index = (virt_addr >> 39) & 0x1FF;
    u64 pdpt_index = (virt_addr >> 30) & 0x1FF;
    u64 pd_index   = (virt_addr >> 21) & 0x1FF;
    u64 pt_index   = (virt_addr >> 12) & 0x1FF;

    u64* pml4 = (u64*)(read_cr3() & PTE_ADDR_MASK);

    u64 pml4e = pml4[pml4_index];
    if (!(pml4e & PTE_PRESENT)) {
        return 0;
    }

    u64* pdpt = (u64*)(pml4e & PTE_ADDR_MASK);

    u64 pdpte = pdpt[pdpt_index];
    if (!(pdpte & PTE_PRESENT)) {
        return 0;
    }

    if (pdpte & PTE_HUGE) {
        return (pdpte & PTE_ADDR_MASK) + (virt_addr & 0x3FFFFFFFULL);
    }

    u64* pd = (u64*)(pdpte & PTE_ADDR_MASK);

    u64 pde = pd[pd_index];
    if (!(pde & PTE_PRESENT)) {
        return 0;
    }

    if (pde & PTE_HUGE) {
        return (pde & PTE_ADDR_MASK) + (virt_addr & 0x1FFFFFULL);
    }

    u64* pt = (u64*)(pde & PTE_ADDR_MASK);

    u64 pte = pt[pt_index];
    if (!(pte & PTE_PRESENT)) {
        return 0;
    }

    return (pte & PTE_ADDR_MASK) + (virt_addr & 0xFFF);
}

void vmm_test_mapping(void) {
    print_color("[VMM map test]\n", COLOR_YELLOW_ON_BLACK);

    u64 virt = VMM_TEST_VIRT;
    u64 phys = pmm_alloc_frame();

    print("test virt = ");
    print_hex64(virt);
    print("\n");

    print("new phys  = ");
    print_hex64(phys);
    print("\n");

    if (phys == 0) {
        print_color("failed to alloc frame for VMM test\n", COLOR_RED_ON_BLACK);
        return;
    }

    vmm_map_page(virt, phys, PTE_WRITABLE);

    u64 mapped = vmm_get_mapping(virt);

    print("mapped to = ");
    print_hex64(mapped);
    print("\n");

    if (mapped != phys) {
        print_color("mapping FAILED\n", COLOR_RED_ON_BLACK);
        return;
    }

    volatile u64* ptr = (volatile u64*)virt;

    ptr[0] = 0x1122334455667788ULL;
    ptr[1] = 0xAABBCCDDEEFF0011ULL;

    print("read [0]  = ");
    print_hex64(ptr[0]);
    print("\n");

    print("read [1]  = ");
    print_hex64(ptr[1]);
    print("\n");

    if (ptr[0] == 0x1122334455667788ULL &&
        ptr[1] == 0xAABBCCDDEEFF0011ULL) {
        print_color("VMM mapping OK\n", COLOR_GREEN_ON_BLACK);
    } else {
        print_color("VMM mapping memory test FAILED\n", COLOR_RED_ON_BLACK);
    }

    print("free frames after VMM test = ");
    print_dec64(pmm_free_frame_count());
    print("\n");
}

void vmm_test_unmap(void) {
    print_color("[VMM unmap test]\n", COLOR_YELLOW_ON_BLACK);

    u64 free_before = pmm_free_frame_count();
    u64 virt = VMM_UNMAP_TEST_VIRT;
    u64 phys = pmm_alloc_frame();

    print("free before = ");
    print_dec64(free_before);
    print("\n");

    print("test virt   = ");
    print_hex64(virt);
    print("\n");

    print("new phys    = ");
    print_hex64(phys);
    print("\n");

    if (phys == 0) {
        print_color("failed to alloc frame for unmap test\n", COLOR_RED_ON_BLACK);
        return;
    }

    vmm_map_page(virt, phys, PTE_WRITABLE);

    u64 mapped = vmm_get_mapping(virt);

    print("mapped to   = ");
    print_hex64(mapped);
    print("\n");

    if (mapped != phys) {
        print_color("mapping before unmap FAILED\n", COLOR_RED_ON_BLACK);
        return;
    }

    volatile u64* ptr = (volatile u64*)virt;
    ptr[0] = 0xCAFEBABE12345678ULL;

    print("read before = ");
    print_hex64(ptr[0]);
    print("\n");

    u64 unmapped_phys = vmm_unmap_page(virt, 1);
    u64 mapped_after = vmm_get_mapping(virt);

    print("unmapped    = ");
    print_hex64(unmapped_phys);
    print("\n");

    print("mapped after= ");
    print_hex64(mapped_after);
    print("\n");

    print("free after  = ");
    print_dec64(pmm_free_frame_count());
    print("\n");

    if (unmapped_phys == phys &&
        mapped_after == 0 &&
        pmm_free_frame_count() == free_before) {
        print_color("VMM unmap OK\n", COLOR_GREEN_ON_BLACK);
    } else {
        print_color("VMM unmap FAILED\n", COLOR_RED_ON_BLACK);
    }
}
