#ifndef MY_OS_HEAP_H
#define MY_OS_HEAP_H

#include "types.h"

/*
 * 현재 커널은 아직 higher-half kernel이 아니므로,
 * heap은 low virtual address 영역에 둔다.
 *
 * 0x40000000은 기존 VMM test에서 사용했으므로 피하고,
 * 0x41000000부터 heap으로 사용한다.
 */
#define KERNEL_HEAP_START        0x0000000041000000ULL
#define KERNEL_HEAP_INITIAL_SIZE 0x0000000000004000ULL
#define KERNEL_HEAP_MAX_SIZE     0x0000000001000000ULL

void heap_init(void);

void* kmalloc(u64 size);
void* kzalloc(u64 size);
void kfree(void* ptr);

void heap_test(void);

#endif