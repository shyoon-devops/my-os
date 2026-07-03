#ifndef MY_OS_KLOG_H
#define MY_OS_KLOG_H

#include "types.h"

#define KLOG_BUFFER_SIZE 8192

void klog_init(void);

void klog_write_char(char c);

void klog_dump(void);
void klog_clear(void);

u32 klog_size(void);
u32 klog_capacity(void);

void klog_register_builtin_commands(void);

#endif