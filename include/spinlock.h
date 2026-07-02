#ifndef MY_OS_SPINLOCK_H
#define MY_OS_SPINLOCK_H

#include "types.h"

#define CPU_RFLAGS_IF 0x0000000000000200ULL

typedef struct spinlock {
    volatile u32 locked;
} spinlock_t;

#define SPINLOCK_INIT { 0 }

void spinlock_init(spinlock_t* lock);

u32 spin_try_lock(spinlock_t* lock);
void spin_lock(spinlock_t* lock);
void spin_unlock(spinlock_t* lock);

u64 irq_save(void);
void irq_restore(u64 flags);

void spin_lock_irqsave(spinlock_t* lock, u64* flags);
void spin_unlock_irqrestore(spinlock_t* lock, u64 flags);

#endif