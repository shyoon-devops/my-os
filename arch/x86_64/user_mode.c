#include "command.h"
#include "console.h"
#include "elf.h"
#include "gdt.h"
#include "heap.h"
#include "pmm.h"
#include "print.h"
#include "types.h"
#include "user_mode.h"
#include "vfs.h"
#include "vmm.h"

#define USER_STACK_SIZE 16384

#define USER_EXEC_PATH_MAX 128
#define USER_EXEC_MIN_VADDR 0x0000000010000000ULL
#define USER_EXEC_MAX_VADDR 0x0000000080000000ULL

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

    pml4[pml4_i] |= PTE_USER | PTE_WRITABLE;

    u64* pdpt = table_from_entry(pml4[pml4_i]);

    if ((pdpt[pdpt_i] & PTE_PRESENT) == 0) {
        return;
    }

    pdpt[pdpt_i] |= PTE_USER | PTE_WRITABLE;

    if (pdpt[pdpt_i] & PTE_HUGE) {
        return;
    }

    u64* pd = table_from_entry(pdpt[pdpt_i]);

    if ((pd[pd_i] & PTE_PRESENT) == 0) {
        return;
    }

    pd[pd_i] |= PTE_USER | PTE_WRITABLE;

    if (pd[pd_i] & PTE_HUGE) {
        return;
    }

    u64* pt = table_from_entry(pd[pd_i]);

    if ((pt[pt_i] & PTE_PRESENT) == 0) {
        return;
    }

    pt[pt_i] |= PTE_USER | PTE_WRITABLE;
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

static u32 user_exec_is_space(char c) {
    return c == ' ' || c == '\t';
}

static u32 user_exec_copy_first_arg(const char* args, char* out, u64 out_size) {
    if (!out || out_size == 0) {
        return 0;
    }

    out[0] = '\0';

    if (!args) {
        return 0;
    }

    while (*args && user_exec_is_space(*args)) {
        args++;
    }

    if (*args == '\0') {
        return 0;
    }

    u64 index = 0;

    while (*args && !user_exec_is_space(*args)) {
        if (index + 1 >= out_size) {
            break;
        }

        out[index] = *args;
        index++;
        args++;
    }

    out[index] = '\0';

    return index > 0;
}

static void user_exec_mem_copy(void* dst, const void* src, u64 size) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;

    for (u64 i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

static void user_exec_zero_page(u64 phys_addr) {
    u8* p = (u8*)phys_addr;

    for (u64 i = 0; i < PAGE_SIZE; i++) {
        p[i] = 0;
    }
}

static u32 user_exec_range_ok(u64 start, u64 size) {
    if (size == 0) {
        return 0;
    }

    if (start < USER_EXEC_MIN_VADDR) {
        return 0;
    }

    if (start >= USER_EXEC_MAX_VADDR) {
        return 0;
    }

    if (size > USER_EXEC_MAX_VADDR - start) {
        return 0;
    }

    return 1;
}

static u32 user_exec_file_range_ok(u64 offset, u64 size, u64 file_size) {
    if (offset > file_size) {
        return 0;
    }

    if (size > file_size - offset) {
        return 0;
    }

    return 1;
}

static u8* user_exec_read_file(const char* path, u64* out_size) {
    if (out_size) {
        *out_size = 0;
    }

    vfs_node_t* node = vfs_lookup(path);

    if (!node || node->type != VFS_NODE_FILE || node->size == 0) {
        return 0;
    }

    u8* buffer = (u8*)kmalloc(node->size);

    if (!buffer) {
        return 0;
    }

    u64 read = vfs_read(node, 0, buffer, node->size);

    if (read != node->size) {
        kfree(buffer);
        return 0;
    }

    if (out_size) {
        *out_size = node->size;
    }

    return buffer;
}

static u32 user_exec_validate_elf(const u8* file, u64 file_size) {
    if (!file || file_size < sizeof(elf64_ehdr_t)) {
        return 0;
    }

    const elf64_ehdr_t* ehdr = (const elf64_ehdr_t*)file;

    if (ehdr->e_ident[0] != ELF_MAGIC0 ||
        ehdr->e_ident[1] != ELF_MAGIC1 ||
        ehdr->e_ident[2] != ELF_MAGIC2 ||
        ehdr->e_ident[3] != ELF_MAGIC3) {
        return 0;
    }

    if (ehdr->e_ident[4] != ELF_CLASS_64 ||
        ehdr->e_ident[5] != ELF_DATA_LSB ||
        ehdr->e_ident[6] != ELF_VERSION_CURRENT) {
        return 0;
    }

    if (ehdr->e_type != ELF_TYPE_EXEC || ehdr->e_machine != ELF_MACHINE_X86_64) {
        return 0;
    }

    if (ehdr->e_version != ELF_VERSION_CURRENT) {
        return 0;
    }

    if (ehdr->e_phentsize != sizeof(elf64_phdr_t) || ehdr->e_phnum == 0) {
        return 0;
    }

    u64 phdr_size = (u64)ehdr->e_phentsize * (u64)ehdr->e_phnum;

    return user_exec_file_range_ok(ehdr->e_phoff, phdr_size, file_size);
}

static void user_exec_unmap_range(u64 start, u64 size) {
    if (size == 0) {
        return;
    }

    u64 begin = align_down(start, PAGE_SIZE);
    u64 end = align_up(start + size, PAGE_SIZE);

    for (u64 addr = begin; addr < end; addr += PAGE_SIZE) {
        if (vmm_get_mapping(addr) != 0) {
            vmm_unmap_page(addr, 1);
        }
    }
}

static u32 user_exec_map_range(u64 start, u64 size) {
    if (!user_exec_range_ok(start, size)) {
        return 0;
    }

    u64 begin = align_down(start, PAGE_SIZE);
    u64 end = align_up(start + size, PAGE_SIZE);

    for (u64 addr = begin; addr < end; addr += PAGE_SIZE) {
        if (vmm_get_mapping(addr) != 0) {
            vmm_unmap_page(addr, 1);
        }

        u64 phys = pmm_alloc_frame();

        if (phys == 0) {
            user_exec_unmap_range(begin, addr - begin);
            return 0;
        }

        user_exec_zero_page(phys);
        vmm_map_page(addr, phys, PTE_USER | PTE_WRITABLE);

        if (vmm_get_mapping(addr) != phys) {
            pmm_free_frame(phys);
            user_exec_unmap_range(begin, addr - begin);
            return 0;
        }
    }

    mark_user_range(begin, end - begin);

    return 1;
}

static s32 user_exec_map_load_segment(
    const u8* file,
    u64 file_size,
    const elf64_phdr_t* phdr,
    u64* mapped_begin,
    u64* mapped_end
) {
    if (phdr->p_type != ELF_PT_LOAD || phdr->p_memsz == 0) {
        return 0;
    }

    if (phdr->p_filesz > phdr->p_memsz) {
        return -1;
    }

    if (!user_exec_file_range_ok(phdr->p_offset, phdr->p_filesz, file_size)) {
        return -1;
    }

    if (!user_exec_range_ok(phdr->p_vaddr, phdr->p_memsz)) {
        return -1;
    }

    u64 begin = align_down(phdr->p_vaddr, PAGE_SIZE);
    u64 end = align_up(phdr->p_vaddr + phdr->p_memsz, PAGE_SIZE);

    if (!user_exec_map_range(phdr->p_vaddr, phdr->p_memsz)) {
        return -1;
    }

    if (phdr->p_filesz > 0) {
        user_exec_mem_copy((void*)phdr->p_vaddr, file + phdr->p_offset, phdr->p_filesz);
    }

    if (*mapped_begin == 0 || begin < *mapped_begin) {
        *mapped_begin = begin;
    }

    if (end > *mapped_end) {
        *mapped_end = end;
    }

    return 0;
}

static s32 user_exec_run_path(const char* path, u64* out_exit_code) {
    if (out_exit_code) {
        *out_exit_code = 0;
    }

    u64 file_size = 0;
    u8* file = user_exec_read_file(path, &file_size);

    if (!file) {
        return -1;
    }

    if (!user_exec_validate_elf(file, file_size)) {
        kfree(file);
        return -1;
    }

    const elf64_ehdr_t* ehdr = (const elf64_ehdr_t*)file;

    if (!user_exec_range_ok(ehdr->e_entry, 1)) {
        kfree(file);
        return -1;
    }

    u64 mapped_begin = 0;
    u64 mapped_end = 0;

    for (u32 i = 0; i < ehdr->e_phnum; i++) {
        const elf64_phdr_t* phdr =
            (const elf64_phdr_t*)(file + ehdr->e_phoff + ((u64)i * ehdr->e_phentsize));

        if (user_exec_map_load_segment(
                file,
                file_size,
                phdr,
                &mapped_begin,
                &mapped_end
            ) != 0) {
            if (mapped_begin != 0 && mapped_end > mapped_begin) {
                user_exec_unmap_range(mapped_begin, mapped_end - mapped_begin);
            }

            kfree(file);
            return -1;
        }
    }

    print("entry = ");
    print_hex64(ehdr->e_entry);
    print("\n");

    u64 entry = ehdr->e_entry;

    kfree(file);

    print("entering user ELF...\n");

    u64 exit_code = user_mode_enter(entry);

    if (mapped_begin != 0 && mapped_end > mapped_begin) {
        user_exec_unmap_range(mapped_begin, mapped_end - mapped_begin);
    }

    if (out_exit_code) {
        *out_exit_code = exit_code;
    }

    return 0;
}

static void user_exec_print_result(const char* command_name, const char* target, u64 exit_code) {
    print("returned from user ELF\n");
    print(command_name);
    print(": ");
    print(target);
    print(" exited with code ");
    print_dec64(exit_code);
    print("\n");
}

static void user_exec_run_command(const char* command_name, const char* target) {
    print("loading user ELF: ");
    print(target);
    print("\n");

    u64 exit_code = 0;

    if (user_exec_run_path(target, &exit_code) != 0) {
        print(command_name);
        print(": failed to run user ELF\n");
        return;
    }

    user_exec_print_result(command_name, target, exit_code);
}

static void cmd_initrun(const char* args) {
    char path[USER_EXEC_PATH_MAX];
    const char* target = "/bin/init";

    if (user_exec_copy_first_arg(args, path, sizeof(path))) {
        target = path;
    }

    user_exec_run_command("initrun", target);
}

static void cmd_exec(const char* args) {
    char path[USER_EXEC_PATH_MAX];

    if (!user_exec_copy_first_arg(args, path, sizeof(path))) {
        print("usage: exec /bin/<program>\n");
        return;
    }

    user_exec_run_command("exec", path);
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
    command_register("initrun", "run /bin/init ELF in ring3", cmd_initrun);
    command_register("exec", "run a user ELF from initramfs", cmd_exec);
}
