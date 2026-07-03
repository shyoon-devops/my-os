#ifndef MY_OS_INITRAMFS_H
#define MY_OS_INITRAMFS_H

#include "types.h"

void initramfs_load(void);

void initramfs_register_builtin_commands(void);

#endif
