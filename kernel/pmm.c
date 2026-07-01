#include "console.h"
#include "multiboot2.h"
#include "pmm.h"
#include "print.h"
#include "utils.h"

typedef struct {
    u64 start;
    u64 end;
} reserved_range_t;

extern char __kernel_start[];
extern char __kernel_end[];

static u8* pmm_bitmap = 0;
static u64 pmm_total_frames = 0;
static u64 pmm_free_frames = 0;
static u64 pmm_bitmap_bytes = 0;
static u64 pmm_bitmap_start = 0;
static u64 pmm_bitmap_end = 0;
static u64 pmm_max_usable_addr = 0;

static u64 pmm_find_max_usable_addr(mb2_tag_mmap_t* mmap);
static u64 pmm_find_free_region(mb2_tag_mmap_t* mmap,
                                reserved_range_t* reserves,
                                u32 reserve_count,
                                u64 size);

static void pmm_mark_available_regions_free(mb2_tag_mmap_t* mmap);
static void pmm_mark_range_used(u64 start, u64 end);
static void pmm_mark_range_free(u64 start, u64 end);
static void pmm_recount_free_frames(void);

static void bitmap_set(u64 frame_index);
static void bitmap_clear(u64 frame_index);
static u32 bitmap_test(u64 frame_index);

void pmm_init(u64 mb2_info_addr) {
    mb2_info_header_t* info = (mb2_info_header_t*)mb2_info_addr;
    mb2_tag_mmap_t* mmap = mb2_find_mmap_tag(mb2_info_addr);

    print_color("[PMM Bitmap Allocator]\n", COLOR_YELLOW_ON_BLACK);

    if (!mmap) {
        print_color("memory map tag not found\n", COLOR_RED_ON_BLACK);
        return;
    }

    u64 kernel_start = align_down_u64((u64)__kernel_start, PAGE_SIZE);
    u64 kernel_end = align_up_u64((u64)__kernel_end, PAGE_SIZE);

    u64 mb2_start = align_down_u64(mb2_info_addr, PAGE_SIZE);
    u64 mb2_end = align_up_u64(mb2_info_addr + info->total_size, PAGE_SIZE);

    reserved_range_t base_reserves[3];

    base_reserves[0].start = 0x00000000ULL;
    base_reserves[0].end   = MIN_USABLE_ADDR;

    base_reserves[1].start = kernel_start;
    base_reserves[1].end   = kernel_end;

    base_reserves[2].start = mb2_start;
    base_reserves[2].end   = mb2_end;

    pmm_max_usable_addr = pmm_find_max_usable_addr(mmap);
    pmm_total_frames = align_up_u64(pmm_max_usable_addr, PAGE_SIZE) / PAGE_SIZE;
    pmm_bitmap_bytes = (pmm_total_frames + 7) / 8;

    u64 bitmap_reserved_size = align_up_u64(pmm_bitmap_bytes, PAGE_SIZE);

    pmm_bitmap_start = pmm_find_free_region(
        mmap,
        base_reserves,
        3,
        bitmap_reserved_size
    );

    if (pmm_bitmap_start == 0) {
        print_color("failed to place PMM bitmap\n", COLOR_RED_ON_BLACK);
        return;
    }

    pmm_bitmap_end = pmm_bitmap_start + bitmap_reserved_size;
    pmm_bitmap = (u8*)pmm_bitmap_start;

    for (u64 i = 0; i < pmm_bitmap_bytes; i++) {
        pmm_bitmap[i] = 0xFF;
    }

    pmm_mark_available_regions_free(mmap);

    pmm_mark_range_used(0x00000000ULL, MIN_USABLE_ADDR);
    pmm_mark_range_used(kernel_start, kernel_end);
    pmm_mark_range_used(mb2_start, mb2_end);
    pmm_mark_range_used(pmm_bitmap_start, pmm_bitmap_end);

    pmm_recount_free_frames();

    print("kernel_start   = ");
    print_hex64(kernel_start);
    print("\n");

    print("kernel_end     = ");
    print_hex64(kernel_end);
    print("\n");

    print("mb2_start      = ");
    print_hex64(mb2_start);
    print("\n");

    print("mb2_end        = ");
    print_hex64(mb2_end);
    print("\n");

    print("max usable addr= ");
    print_hex64(pmm_max_usable_addr);
    print("\n");

    print("total frames   = ");
    print_dec64(pmm_total_frames);
    print("\n");

    print("bitmap bytes   = ");
    print_dec64(pmm_bitmap_bytes);
    print("\n");

    print("bitmap start   = ");
    print_hex64(pmm_bitmap_start);
    print("\n");

    print("bitmap end     = ");
    print_hex64(pmm_bitmap_end);
    print("\n");

    print("free frames    = ");
    print_dec64(pmm_free_frames);
    print("\n");
}

static u64 pmm_find_max_usable_addr(mb2_tag_mmap_t* mmap) {
    u64 max_addr = 0;

    u64 entries_start = (u64)&mmap->entries[0];
    u64 entries_end = (u64)mmap + mmap->size;

    for (u64 entry_addr = entries_start;
         entry_addr + sizeof(mb2_mmap_entry_t) <= entries_end;
         entry_addr += mmap->entry_size) {
        mb2_mmap_entry_t* entry = (mb2_mmap_entry_t*)entry_addr;

        if (entry->type != 1) {
            continue;
        }

        u64 start = align_up_u64(max_u64(entry->addr, MIN_USABLE_ADDR), PAGE_SIZE);
        u64 end = align_down_u64(entry->addr + entry->len, PAGE_SIZE);

        if (end <= start) {
            continue;
        }

        if (end > max_addr) {
            max_addr = end;
        }
    }

    return max_addr;
}

static u64 pmm_find_free_region(mb2_tag_mmap_t* mmap,
                                reserved_range_t* reserves,
                                u32 reserve_count,
                                u64 size) {
    size = align_up_u64(size, PAGE_SIZE);

    u64 entries_start = (u64)&mmap->entries[0];
    u64 entries_end = (u64)mmap + mmap->size;

    for (u64 entry_addr = entries_start;
         entry_addr + sizeof(mb2_mmap_entry_t) <= entries_end;
         entry_addr += mmap->entry_size) {
        mb2_mmap_entry_t* entry = (mb2_mmap_entry_t*)entry_addr;

        if (entry->type != 1) {
            continue;
        }

        u64 start = align_up_u64(max_u64(entry->addr, MIN_USABLE_ADDR), PAGE_SIZE);
        u64 end = align_down_u64(entry->addr + entry->len, PAGE_SIZE);

        if (end <= start) {
            continue;
        }

        u64 cursor = start;

        while (cursor + size <= end) {
            u32 collided = 0;

            for (u32 i = 0; i < reserve_count; i++) {
                if (ranges_overlap(cursor, cursor + size,
                                   reserves[i].start, reserves[i].end)) {
                    cursor = align_up_u64(reserves[i].end, PAGE_SIZE);
                    collided = 1;
                    break;
                }
            }

            if (!collided) {
                return cursor;
            }
        }
    }

    return 0;
}

static void pmm_mark_available_regions_free(mb2_tag_mmap_t* mmap) {
    u64 entries_start = (u64)&mmap->entries[0];
    u64 entries_end = (u64)mmap + mmap->size;

    for (u64 entry_addr = entries_start;
         entry_addr + sizeof(mb2_mmap_entry_t) <= entries_end;
         entry_addr += mmap->entry_size) {
        mb2_mmap_entry_t* entry = (mb2_mmap_entry_t*)entry_addr;

        if (entry->type != 1) {
            continue;
        }

        u64 start = align_up_u64(max_u64(entry->addr, MIN_USABLE_ADDR), PAGE_SIZE);
        u64 end = align_down_u64(entry->addr + entry->len, PAGE_SIZE);

        pmm_mark_range_free(start, end);
    }
}

static void pmm_mark_range_used(u64 start, u64 end) {
    start = align_down_u64(start, PAGE_SIZE);
    end = align_up_u64(end, PAGE_SIZE);

    if (end <= start) {
        return;
    }

    u64 first_frame = start / PAGE_SIZE;
    u64 last_frame = end / PAGE_SIZE;

    if (last_frame > pmm_total_frames) {
        last_frame = pmm_total_frames;
    }

    for (u64 frame = first_frame; frame < last_frame; frame++) {
        bitmap_set(frame);
    }
}

static void pmm_mark_range_free(u64 start, u64 end) {
    start = align_up_u64(start, PAGE_SIZE);
    end = align_down_u64(end, PAGE_SIZE);

    if (end <= start) {
        return;
    }

    u64 first_frame = start / PAGE_SIZE;
    u64 last_frame = end / PAGE_SIZE;

    if (last_frame > pmm_total_frames) {
        last_frame = pmm_total_frames;
    }

    for (u64 frame = first_frame; frame < last_frame; frame++) {
        bitmap_clear(frame);
    }
}

static void pmm_recount_free_frames(void) {
    pmm_free_frames = 0;

    for (u64 frame = 0; frame < pmm_total_frames; frame++) {
        if (!bitmap_test(frame)) {
            pmm_free_frames++;
        }
    }
}

u64 pmm_alloc_frame(void) {
    if (!pmm_bitmap) {
        return 0;
    }

    for (u64 frame = 0; frame < pmm_total_frames; frame++) {
        if (!bitmap_test(frame)) {
            bitmap_set(frame);

            if (pmm_free_frames > 0) {
                pmm_free_frames--;
            }

            return frame * PAGE_SIZE;
        }
    }

    return 0;
}

void pmm_free_frame(u64 frame_addr) {
    if (!pmm_bitmap) {
        return;
    }

    if ((frame_addr % PAGE_SIZE) != 0) {
        return;
    }

    u64 frame = frame_addr / PAGE_SIZE;

    if (frame >= pmm_total_frames) {
        return;
    }

    if (bitmap_test(frame)) {
        bitmap_clear(frame);
        pmm_free_frames++;
    }
}

u64 pmm_free_frame_count(void) {
    return pmm_free_frames;
}

static void bitmap_set(u64 frame_index) {
    u64 byte_index = frame_index / 8;
    u64 bit_index = frame_index % 8;

    pmm_bitmap[byte_index] |= (u8)(1u << bit_index);
}

static void bitmap_clear(u64 frame_index) {
    u64 byte_index = frame_index / 8;
    u64 bit_index = frame_index % 8;

    pmm_bitmap[byte_index] &= (u8)~(1u << bit_index);
}

static u32 bitmap_test(u64 frame_index) {
    u64 byte_index = frame_index / 8;
    u64 bit_index = frame_index % 8;

    return (pmm_bitmap[byte_index] & (u8)(1u << bit_index)) != 0;
}

void pmm_test(void) {
    print_color("[PMM alloc/free test]\n", COLOR_YELLOW_ON_BLACK);

    print("free before = ");
    print_dec64(pmm_free_frames);
    print("\n");

    u64 a = pmm_alloc_frame();
    u64 b = pmm_alloc_frame();
    u64 c = pmm_alloc_frame();

    print("alloc A = ");
    print_hex64(a);
    print("\n");

    print("alloc B = ");
    print_hex64(b);
    print("\n");

    print("alloc C = ");
    print_hex64(c);
    print("\n");

    print("free after 3 allocs = ");
    print_dec64(pmm_free_frames);
    print("\n");

    pmm_free_frame(b);

    print("freed B = ");
    print_hex64(b);
    print("\n");

    print("free after free B = ");
    print_dec64(pmm_free_frames);
    print("\n");

    u64 d = pmm_alloc_frame();

    print("alloc D = ");
    print_hex64(d);
    print("\n");

    if (d == b) {
        print_color("reuse freed frame OK\n", COLOR_GREEN_ON_BLACK);
    } else {
        print_color("reuse freed frame unexpected\n", COLOR_RED_ON_BLACK);
    }

    print("free final = ");
    print_dec64(pmm_free_frames);
    print("\n");

    (void)a;
    (void)c;
}
