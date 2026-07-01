#include "spinlock.h"
#include "types.h"

static inline void cpu_pause(void) {
    __asm__ volatile ("pause");
}

static inline u32 atomic_xchg_u32(volatile u32* ptr, u32 value) {
    __asm__ volatile (
        "xchgl %0, %1"
        : "+r"(value), "+m"(*ptr)
        :
        : "memory"
    );

    return value;
}

void spinlock_init(spinlock_t* lock) {
    lock->locked = 0;
}

u32 spin_try_lock(spinlock_t* lock) {
    /*
     * xchg는 x86에서 memory operand와 함께 쓰면 암묵적으로 atomic이다.
     *
     * 이전 값이 0이면 lock 획득 성공.
     * 이전 값이 1이면 이미 누가 잡고 있던 것.
     */
    return atomic_xchg_u32(&lock->locked, 1) == 0;
}

void spin_lock(spinlock_t* lock) {
    while (!spin_try_lock(lock)) {
        while (lock->locked) {
            cpu_pause();
        }
    }
}

void spin_unlock(spinlock_t* lock) {
    __asm__ volatile ("" ::: "memory");
    lock->locked = 0;
    __asm__ volatile ("" ::: "memory");
}

u64 irq_save(void) {
    u64 flags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory"
    );

    return flags;
}

void irq_restore(u64 flags) {
    if (flags & CPU_RFLAGS_IF) {
        __asm__ volatile ("sti" ::: "memory");
    } else {
        __asm__ volatile ("cli" ::: "memory");
    }
}

void spin_lock_irqsave(spinlock_t* lock, u64* flags) {
    *flags = irq_save();
    spin_lock(lock);
}

void spin_unlock_irqrestore(spinlock_t* lock, u64 flags) {
    spin_unlock(lock);
    irq_restore(flags);
}