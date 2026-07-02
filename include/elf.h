#ifndef MY_OS_ELF_H
#define MY_OS_ELF_H

#include "types.h"

#define ELF_MAGIC0 0x7F
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'

#define ELF_CLASS_64 2
#define ELF_DATA_LSB 1
#define ELF_VERSION_CURRENT 1

#define ELF_TYPE_EXEC 2
#define ELF_TYPE_DYN  3

#define ELF_MACHINE_X86_64 62

#define ELF_PT_NULL 0
#define ELF_PT_LOAD 1

#define ELF_PF_X 1
#define ELF_PF_W 2
#define ELF_PF_R 4

typedef struct elf64_ehdr {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

typedef struct elf64_phdr {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} __attribute__((packed)) elf64_phdr_t;

typedef struct elf_loaded_segment {
    u64 vaddr;
    u64 memsz;
    u64 filesz;
    u32 flags;
    void* memory;
} elf_loaded_segment_t;

#define ELF_MAX_LOADED_SEGMENTS 8

typedef struct elf_loaded_image {
    u32 valid;
    u64 entry;
    u32 segment_count;
    u64 total_memory;

    elf_loaded_segment_t segments[ELF_MAX_LOADED_SEGMENTS];
} elf_loaded_image_t;

s32 elf_load_from_path(const char* path, elf_loaded_image_t* out);

void elf_register_builtin_commands(void);

#endif
