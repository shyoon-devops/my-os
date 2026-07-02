#ifndef MY_OS_UTILS_H
#define MY_OS_UTILS_H

#include "types.h"

u64 align_up_u64(u64 value, u64 align);
u64 align_down_u64(u64 value, u64 align);
u64 max_u64(u64 a, u64 b);
u64 min_u64(u64 a, u64 b);
u32 ranges_overlap(u64 a_start, u64 a_end, u64 b_start, u64 b_end);

#endif
