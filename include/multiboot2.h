#ifndef MY_OS_MULTIBOOT2_H
#define MY_OS_MULTIBOOT2_H

#include "types.h"

#define MB2_BOOTLOADER_MAGIC 0x36D76289u

#define MB2_TAG_TYPE_END             0
#define MB2_TAG_TYPE_CMDLINE         1
#define MB2_TAG_TYPE_BOOT_LOADER     2
#define MB2_TAG_TYPE_MMAP            6
#define MB2_TAG_TYPE_IMAGE_LOAD_BASE 21

typedef struct {
    u32 total_size;
    u32 reserved;
} mb2_info_header_t;

typedef struct {
    u32 type;
    u32 size;
} mb2_tag_t;

typedef struct {
    u32 type;
    u32 size;
    char string[];
} mb2_tag_string_t;

typedef struct {
    u64 addr;
    u64 len;
    u32 type;
    u32 zero;
} mb2_mmap_entry_t;

typedef struct {
    u32 type;
    u32 size;
    u32 entry_size;
    u32 entry_version;
    mb2_mmap_entry_t entries[];
} mb2_tag_mmap_t;

void mb2_dump_info(u64 mb2_info_addr);
mb2_tag_mmap_t* mb2_find_mmap_tag(u64 mb2_info_addr);
const char* mb2_mmap_type_name(u32 type);

#endif
