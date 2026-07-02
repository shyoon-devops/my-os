#include "command.h"
#include "elf.h"
#include "heap.h"
#include "print.h"
#include "types.h"
#include "vfs.h"

#define ELF_CMD_PATH_MAX 128

static elf_loaded_image_t last_loaded_image;

static u32 is_space(char c) {
    return c == ' ' || c == '\t';
}

static u32 copy_first_arg(const char* args, char* out, u64 out_size) {
    if (!out || out_size == 0) {
        return 0;
    }

    out[0] = '\0';

    if (!args) {
        return 0;
    }

    while (*args && is_space(*args)) {
        args++;
    }

    if (*args == '\0') {
        return 0;
    }

    u64 index = 0;

    while (*args && !is_space(*args)) {
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

static void mem_zero(void* dst, u64 size) {
    u8* d = (u8*)dst;

    for (u64 i = 0; i < size; i++) {
        d[i] = 0;
    }
}

static void mem_copy(void* dst, const void* src, u64 size) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;

    for (u64 i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

static void elf_clear_image(elf_loaded_image_t* image) {
    if (!image) {
        return;
    }

    image->valid = 0;
    image->entry = 0;
    image->segment_count = 0;
    image->total_memory = 0;

    for (u32 i = 0; i < ELF_MAX_LOADED_SEGMENTS; i++) {
        image->segments[i].vaddr = 0;
        image->segments[i].memsz = 0;
        image->segments[i].filesz = 0;
        image->segments[i].flags = 0;
        image->segments[i].memory = 0;
    }
}

static u32 elf_range_ok(u64 offset, u64 size, u64 file_size) {
    if (offset > file_size) {
        return 0;
    }

    if (size > file_size - offset) {
        return 0;
    }

    return 1;
}

static u32 elf_validate_header(const u8* file, u64 file_size) {
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

    if (ehdr->e_ident[4] != ELF_CLASS_64) {
        return 0;
    }

    if (ehdr->e_ident[5] != ELF_DATA_LSB) {
        return 0;
    }

    if (ehdr->e_ident[6] != ELF_VERSION_CURRENT) {
        return 0;
    }

    if (ehdr->e_type != ELF_TYPE_EXEC && ehdr->e_type != ELF_TYPE_DYN) {
        return 0;
    }

    if (ehdr->e_machine != ELF_MACHINE_X86_64) {
        return 0;
    }

    if (ehdr->e_version != ELF_VERSION_CURRENT) {
        return 0;
    }

    if (ehdr->e_phentsize != sizeof(elf64_phdr_t)) {
        return 0;
    }

    if (ehdr->e_phnum == 0) {
        return 0;
    }

    u64 phdr_size = (u64)ehdr->e_phentsize * (u64)ehdr->e_phnum;

    if (!elf_range_ok(ehdr->e_phoff, phdr_size, file_size)) {
        return 0;
    }

    return 1;
}

static const char* elf_type_name(u16 type) {
    switch (type) {
        case ELF_TYPE_EXEC:
            return "EXEC";
        case ELF_TYPE_DYN:
            return "DYN";
        default:
            return "UNKNOWN";
    }
}

static const char* phdr_type_name(u32 type) {
    switch (type) {
        case ELF_PT_NULL:
            return "NULL";
        case ELF_PT_LOAD:
            return "LOAD";
        default:
            return "OTHER";
    }
}

static void print_segment_flags(u32 flags) {
    print((flags & ELF_PF_R) ? "R" : "-");
    print((flags & ELF_PF_W) ? "W" : "-");
    print((flags & ELF_PF_X) ? "X" : "-");
}

static u8* read_file_to_memory(const char* path, u64* out_size) {
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

static void print_elf_summary(const u8* file, u64 file_size) {
    const elf64_ehdr_t* ehdr = (const elf64_ehdr_t*)file;

    print("ELF64 ");
    print(elf_type_name(ehdr->e_type));
    print(" x86_64\n");

    print("entry = ");
    print_hex64(ehdr->e_entry);
    print("\n");

    print("program headers = ");
    print_dec64(ehdr->e_phnum);
    print("\n");

    print("file size = ");
    print_dec64(file_size);
    print("\n");

    for (u32 i = 0; i < ehdr->e_phnum; i++) {
        const elf64_phdr_t* phdr =
            (const elf64_phdr_t*)(file + ehdr->e_phoff + ((u64)i * ehdr->e_phentsize));

        print("  phdr ");
        print_dec64(i);
        print(" type=");
        print(phdr_type_name(phdr->p_type));

        print(" flags=");
        print_segment_flags(phdr->p_flags);

        print(" off=");
        print_hex64(phdr->p_offset);

        print(" vaddr=");
        print_hex64(phdr->p_vaddr);

        print(" filesz=");
        print_dec64(phdr->p_filesz);

        print(" memsz=");
        print_dec64(phdr->p_memsz);

        print("\n");
    }
}

s32 elf_load_from_path(const char* path, elf_loaded_image_t* out) {
    if (!path || !out) {
        return -1;
    }

    elf_clear_image(out);

    u64 file_size = 0;
    u8* file = read_file_to_memory(path, &file_size);

    if (!file) {
        return -1;
    }

    if (!elf_validate_header(file, file_size)) {
        kfree(file);
        return -1;
    }

    const elf64_ehdr_t* ehdr = (const elf64_ehdr_t*)file;

    out->entry = ehdr->e_entry;

    for (u32 i = 0; i < ehdr->e_phnum; i++) {
        const elf64_phdr_t* phdr =
            (const elf64_phdr_t*)(file + ehdr->e_phoff + ((u64)i * ehdr->e_phentsize));

        if (phdr->p_type != ELF_PT_LOAD) {
            continue;
        }

        if (phdr->p_memsz == 0) {
            continue;
        }

        if (phdr->p_filesz > phdr->p_memsz) {
            kfree(file);
            elf_clear_image(out);
            return -1;
        }

        if (!elf_range_ok(phdr->p_offset, phdr->p_filesz, file_size)) {
            kfree(file);
            elf_clear_image(out);
            return -1;
        }

        if (out->segment_count >= ELF_MAX_LOADED_SEGMENTS) {
            kfree(file);
            elf_clear_image(out);
            return -1;
        }

        void* memory = kmalloc(phdr->p_memsz);

        if (!memory) {
            kfree(file);
            elf_clear_image(out);
            return -1;
        }

        mem_zero(memory, phdr->p_memsz);

        if (phdr->p_filesz > 0) {
            mem_copy(memory, file + phdr->p_offset, phdr->p_filesz);
        }

        elf_loaded_segment_t* segment = &out->segments[out->segment_count];

        segment->vaddr = phdr->p_vaddr;
        segment->memsz = phdr->p_memsz;
        segment->filesz = phdr->p_filesz;
        segment->flags = phdr->p_flags;
        segment->memory = memory;

        out->segment_count++;
        out->total_memory += phdr->p_memsz;
    }

    out->valid = 1;

    kfree(file);

    return 0;
}

static void cmd_elfinfo(const char* args) {
    char path[ELF_CMD_PATH_MAX];

    if (!copy_first_arg(args, path, sizeof(path))) {
        print("usage: elfinfo <path>\n");
        return;
    }

    u64 file_size = 0;
    u8* file = read_file_to_memory(path, &file_size);

    if (!file) {
        print("elfinfo: cannot read file: ");
        print(path);
        print("\n");
        return;
    }

    if (!elf_validate_header(file, file_size)) {
        print("elfinfo: invalid or unsupported ELF: ");
        print(path);
        print("\n");
        kfree(file);
        return;
    }

    print_elf_summary(file, file_size);

    kfree(file);
}

static void cmd_elfload(const char* args) {
    char path[ELF_CMD_PATH_MAX];

    if (!copy_first_arg(args, path, sizeof(path))) {
        print("usage: elfload <path>\n");
        return;
    }

    if (elf_load_from_path(path, &last_loaded_image) != 0) {
        print("elfload: failed: ");
        print(path);
        print("\n");
        return;
    }

    print("ELF loaded: ");
    print(path);
    print("\n");

    print("entry = ");
    print_hex64(last_loaded_image.entry);
    print("\n");

    print("loaded segments = ");
    print_dec64(last_loaded_image.segment_count);
    print("\n");

    print("loaded memory = ");
    print_dec64(last_loaded_image.total_memory);
    print(" bytes\n");

    for (u32 i = 0; i < last_loaded_image.segment_count; i++) {
        elf_loaded_segment_t* segment = &last_loaded_image.segments[i];

        print("  segment ");
        print_dec64(i);

        print(" vaddr=");
        print_hex64(segment->vaddr);

        print(" mem=");
        print_hex64((u64)segment->memory);

        print(" filesz=");
        print_dec64(segment->filesz);

        print(" memsz=");
        print_dec64(segment->memsz);

        print(" flags=");
        print_segment_flags(segment->flags);

        print("\n");
    }
}

static void cmd_elflast(const char* args) {
    (void)args;

    if (!last_loaded_image.valid) {
        print("no ELF image loaded\n");
        return;
    }

    print("last ELF image:\n");

    print("entry = ");
    print_hex64(last_loaded_image.entry);
    print("\n");

    print("segments = ");
    print_dec64(last_loaded_image.segment_count);
    print("\n");

    print("total memory = ");
    print_dec64(last_loaded_image.total_memory);
    print("\n");
}

void elf_register_builtin_commands(void) {
    elf_clear_image(&last_loaded_image);

    command_register("elfinfo", "show ELF64 header and program headers", cmd_elfinfo);
    command_register("elfload", "load ELF64 PT_LOAD segments into memory", cmd_elfload);
    command_register("elflast", "show last loaded ELF image", cmd_elflast);
}
