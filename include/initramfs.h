#ifndef MY_OS_INITRAMFS_H
#define MY_OS_INITRAMFS_H

#include "types.h"

void initramfs_load(void);

u32 initramfs_loaded_file_count(void);
u32 initramfs_loaded_dir_count(void);
u64 initramfs_loaded_byte_count(void);

void initramfs_register_builtin_commands(void);

#endif
