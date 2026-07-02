#include "command.h"
#include "elf.h"
#include "heap.h"
#include "pmm.h"
#include "print.h"
#include "types.h"
#include "user_exec.h"
#include "user_mode.h"
#include "utils.h"
#include "vfs.h"
#include "vmm.h"

#define USER_EXEC_PATH_MAX 128
#define USER_EXEC_MIN_VADDR 0x0000000010000000ULL
#define USER_EXEC_MAX_VADDR 0x0000000080000000ULL

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

    if (!path) {
        return 0;
    }

    vfs_node_t* node = vfs_lookup(path);

    if (!node || node->type != VFS_NODE_FILE) {
        return 0;
    }

    if (node->size == 0) {
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
    if (!file) {
        return 0;
    }

    if (file_size < sizeof(elf64_ehdr_t)) {
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

    if (ehdr->e_type != ELF_TYPE_EXEC) {
        return 0;
    }

    if (ehdr->e_machine != ELF_MACHINE_X86_64) {
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

    u64 begin = align_down_u64(start, PAGE_SIZE);
    u64 end = align_up_u64(start + size, PAGE_SIZE);

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

    u64 begin = align_down_u64(start, PAGE_SIZE);
    u64 end = align_up_u64(start + size, PAGE_SIZE);

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

    user_mode_mark_user_range(begin, end - begin);

    return 1;
}

static s32 user_exec_map_load_segment(
    const u8* file,
    u64 file_size,
    const elf64_phdr_t* phdr,
    u64* mapped_begin,
    u64* mapped_end
) {
    if (!file || !phdr || !mapped_begin || !mapped_end) {
        return -1;
    }

    if (phdr->p_type != ELF_PT_LOAD) {
        return 0;
    }

    if (phdr->p_memsz == 0) {
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

    u64 begin = align_down_u64(phdr->p_vaddr, PAGE_SIZE);
    u64 end = align_up_u64(phdr->p_vaddr + phdr->p_memsz, PAGE_SIZE);

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

static void cmd_initrun(const char* args) {
    char path[USER_EXEC_PATH_MAX];
    const char* target = "/bin/init";

    if (user_exec_copy_first_arg(args, path, sizeof(path))) {
        target = path;
    }

    print("loading user ELF: ");
    print(target);
    print("\n");

    u64 exit_code = 0;

    if (user_exec_run_path(target, &exit_code) != 0) {
        print("initrun: failed to run user ELF\n");
        return;
    }

    print("returned from user ELF\n");
    print("exit code = ");
    print_dec64(exit_code);
    print("\n");
}

void user_exec_register_builtin_commands(void) {
    command_register("initrun", "run /bin/init ELF in ring3", cmd_initrun);
}
