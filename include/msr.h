#ifndef MY_OS_MSR_H
#define MY_OS_MSR_H

#include "types.h"

#define MSR_IA32_EFER  0xC0000080u
#define MSR_IA32_STAR  0xC0000081u
#define MSR_IA32_LSTAR 0xC0000082u
#define MSR_IA32_FMASK 0xC0000084u

#define IA32_EFER_SCE  0x0000000000000001ull

static inline u64 rdmsr(u32 msr) {
    u32 lo;
    u32 hi;

    __asm__ volatile (
        "rdmsr"
        : "=a"(lo), "=d"(hi)
        : "c"(msr)
    );

    return ((u64)hi << 32) | (u64)lo;
}

static inline void wrmsr(u32 msr, u64 value) {
    u32 lo = (u32)(value & 0xFFFFFFFFu);
    u32 hi = (u32)(value >> 32);

    __asm__ volatile (
        "wrmsr"
        :
        : "c"(msr), "a"(lo), "d"(hi)
    );
}

#endif
