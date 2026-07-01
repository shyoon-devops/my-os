#include "utils.h"

u64 align_up_u64(u64 value, u64 align) {
    return (value + align - 1) & ~(align - 1);
}

u64 align_down_u64(u64 value, u64 align) {
    return value & ~(align - 1);
}

u64 max_u64(u64 a, u64 b) {
    if (a > b) {
        return a;
    }

    return b;
}

u64 min_u64(u64 a, u64 b) {
    if (a < b) {
        return a;
    }

    return b;
}

u32 ranges_overlap(u64 a_start, u64 a_end, u64 b_start, u64 b_end) {
    u64 start = max_u64(a_start, b_start);
    u64 end = min_u64(a_end, b_end);

    return start < end;
}
