#include "console.h"
#include "heap.h"
#include "pmm.h"
#include "print.h"
#include "types.h"
#include "vmm.h"

#define HEAP_ALIGN       16ULL
#define HEAP_MIN_SPLIT   32ULL
#define HEAP_BLOCK_MAGIC 0x48454150424C4B30ULL

/*
 * vmm_map_page()에 넘길 기본 page flags.
 *
 * bit 0 = present
 * bit 1 = writable
 */
#define HEAP_PAGE_FLAGS 0x03ULL

typedef struct heap_block {
    u64 magic;
    u64 size;
    u64 free;

    struct heap_block* prev;
    struct heap_block* next;

    /*
     * heap_block 크기를 16-byte alignment에 맞추기 위한 padding.
     * 현재 구조체 크기는 48 bytes가 된다.
     */
    u64 reserved;
} heap_block_t;

static heap_block_t* heap_first = 0;

static u64 heap_start = KERNEL_HEAP_START;
static u64 heap_mapped_end = KERNEL_HEAP_START;
static u64 heap_max_end = KERNEL_HEAP_START + KERNEL_HEAP_MAX_SIZE;

static u32 heap_initialized = 0;

static u64 align_up(u64 value, u64 align) {
    return (value + align - 1) & ~(align - 1);
}

static void heap_memset(void* ptr, u8 value, u64 size) {
    u8* p = (u8*)ptr;

    for (u64 i = 0; i < size; i++) {
        p[i] = value;
    }
}

static void heap_init_block(heap_block_t* block, u64 size, u64 free) {
    block->magic = HEAP_BLOCK_MAGIC;
    block->size = size;
    block->free = free;
    block->prev = 0;
    block->next = 0;
    block->reserved = 0;
}

static u32 heap_map_pages(u64 virt_start, u64 size) {
    u64 map_size = align_up(size, PAGE_SIZE);
    u64 mapped = 0;

    while (mapped < map_size) {
        u64 virt = virt_start + mapped;
        u64 phys = pmm_alloc_frame();

        if (phys == 0) {
            /*
             * 이미 매핑한 page는 되돌린다.
             */
            while (mapped > 0) {
                mapped -= PAGE_SIZE;
                vmm_unmap_page(virt_start + mapped, 1);
            }

            return 0;
        }

        /*
         * 현재 우리 vmm_map_page()는 void 반환이다.
         * 그래서 성공/실패를 if로 검사하지 않고 호출만 한다.
         *
         * 지금 단계에서는 PMM에서 physical frame만 정상 할당되면
         * mapping은 성공한다고 본다.
         */
        vmm_map_page(virt, phys, HEAP_PAGE_FLAGS);

        mapped += PAGE_SIZE;
    }

    return 1;
}

static heap_block_t* heap_find_free_block(u64 size) {
    heap_block_t* current = heap_first;

    while (current) {
        if (current->magic == HEAP_BLOCK_MAGIC &&
            current->free &&
            current->size >= size) {
            return current;
        }

        current = current->next;
    }

    return 0;
}

static void heap_split_block(heap_block_t* block, u64 size) {
    if (!block) {
        return;
    }

    if (block->size < size) {
        return;
    }

    u64 remaining = block->size - size;

    /*
     * 남는 공간이 새 block header + 최소 payload보다 작으면 쪼개지 않는다.
     */
    if (remaining < sizeof(heap_block_t) + HEAP_MIN_SPLIT) {
        return;
    }

    heap_block_t* new_block =
        (heap_block_t*)((u8*)block + sizeof(heap_block_t) + size);

    heap_init_block(new_block, remaining - sizeof(heap_block_t), 1);

    new_block->prev = block;
    new_block->next = block->next;

    if (block->next) {
        block->next->prev = new_block;
    }

    block->next = new_block;
    block->size = size;
}

static void heap_coalesce_with_next(heap_block_t* block) {
    if (!block) {
        return;
    }

    heap_block_t* next = block->next;

    if (!next) {
        return;
    }

    if (next->magic != HEAP_BLOCK_MAGIC) {
        return;
    }

    if (!next->free) {
        return;
    }

    block->size = block->size + sizeof(heap_block_t) + next->size;
    block->next = next->next;

    if (next->next) {
        next->next->prev = block;
    }
}

static void heap_coalesce(heap_block_t* block) {
    if (!block) {
        return;
    }

    /*
     * 뒤쪽 free block과 먼저 합친다.
     */
    heap_coalesce_with_next(block);

    /*
     * 앞쪽 block도 free면 앞쪽 block 기준으로 다시 합친다.
     */
    if (block->prev && block->prev->free) {
        heap_coalesce_with_next(block->prev);
    }
}

static heap_block_t* heap_last_block(void) {
    heap_block_t* current = heap_first;

    if (!current) {
        return 0;
    }

    while (current->next) {
        current = current->next;
    }

    return current;
}

static u32 heap_expand(u64 min_payload_size) {
    u64 needed = sizeof(heap_block_t) + min_payload_size;
    u64 expand_size = align_up(needed, PAGE_SIZE);

    if (expand_size < KERNEL_HEAP_INITIAL_SIZE) {
        expand_size = KERNEL_HEAP_INITIAL_SIZE;
    }

    if (heap_mapped_end + expand_size > heap_max_end) {
        return 0;
    }

    u64 new_region_start = heap_mapped_end;

    if (!heap_map_pages(new_region_start, expand_size)) {
        return 0;
    }

    heap_mapped_end += expand_size;

    heap_block_t* new_block = (heap_block_t*)new_region_start;
    heap_init_block(new_block, expand_size - sizeof(heap_block_t), 1);

    if (!heap_first) {
        heap_first = new_block;
        return 1;
    }

    heap_block_t* last = heap_last_block();

    last->next = new_block;
    new_block->prev = last;

    /*
     * 기존 마지막 block이 free였다면 새로 확장된 block과 합친다.
     */
    if (last->free) {
        heap_coalesce_with_next(last);
    }

    return 1;
}

static u64 heap_total_free_bytes(void) {
    u64 total = 0;
    heap_block_t* current = heap_first;

    while (current) {
        if (current->magic == HEAP_BLOCK_MAGIC && current->free) {
            total += current->size;
        }

        current = current->next;
    }

    return total;
}

static u64 heap_largest_free_block(void) {
    u64 largest = 0;
    heap_block_t* current = heap_first;

    while (current) {
        if (current->magic == HEAP_BLOCK_MAGIC &&
            current->free &&
            current->size > largest) {
            largest = current->size;
        }

        current = current->next;
    }

    return largest;
}

static u64 heap_block_count(void) {
    u64 count = 0;
    heap_block_t* current = heap_first;

    while (current) {
        count++;
        current = current->next;
    }

    return count;
}

void heap_init(void) {
    heap_first = 0;
    heap_start = KERNEL_HEAP_START;
    heap_mapped_end = KERNEL_HEAP_START;
    heap_max_end = KERNEL_HEAP_START + KERNEL_HEAP_MAX_SIZE;
    heap_initialized = 0;

    if (!heap_expand(KERNEL_HEAP_INITIAL_SIZE - sizeof(heap_block_t))) {
        print_color("Heap initialization FAILED\n", COLOR_RED_ON_BLACK);
        return;
    }

    heap_initialized = 1;

    print_color("Kernel heap initialized\n", COLOR_GREEN_ON_BLACK);

    print("heap start = ");
    print_hex64(heap_start);
    print("\n");

    print("heap end   = ");
    print_hex64(heap_mapped_end);
    print("\n");

    print("heap max   = ");
    print_hex64(heap_max_end);
    print("\n");

    print("heap free  = ");
    print_dec64(heap_total_free_bytes());
    print(" bytes\n");
}

void* kmalloc(u64 size) {
    if (!heap_initialized) {
        return 0;
    }

    if (size == 0) {
        return 0;
    }

    size = align_up(size, HEAP_ALIGN);

    heap_block_t* block = heap_find_free_block(size);

    if (!block) {
        if (!heap_expand(size)) {
            return 0;
        }

        block = heap_find_free_block(size);

        if (!block) {
            return 0;
        }
    }

    heap_split_block(block, size);

    block->free = 0;

    return (void*)((u8*)block + sizeof(heap_block_t));
}

void* kzalloc(u64 size) {
    void* ptr = kmalloc(size);

    if (!ptr) {
        return 0;
    }

    heap_memset(ptr, 0, size);

    return ptr;
}

void kfree(void* ptr) {
    if (!ptr) {
        return;
    }

    heap_block_t* block =
        (heap_block_t*)((u8*)ptr - sizeof(heap_block_t));

    if (block->magic != HEAP_BLOCK_MAGIC) {
        print_color("kfree invalid pointer\n", COLOR_RED_ON_BLACK);
        return;
    }

    if (block->free) {
        print_color("kfree double free ignored\n", COLOR_RED_ON_BLACK);
        return;
    }

    block->free = 1;

    heap_coalesce(block);
}

void heap_test(void) {
    print_color("[Heap test]\n", COLOR_YELLOW_ON_BLACK);

    print("block header size = ");
    print_dec64(sizeof(heap_block_t));
    print(" bytes\n");

    print("free before = ");
    print_dec64(heap_total_free_bytes());
    print(" bytes\n");

    void* a = kmalloc(32);
    void* b = kmalloc(128);
    void* c = kmalloc(4096);

    print("alloc A 32    = ");
    print_hex64((u64)a);
    print("\n");

    print("alloc B 128   = ");
    print_hex64((u64)b);
    print("\n");

    print("alloc C 4096  = ");
    print_hex64((u64)c);
    print("\n");

    if (!a || !b || !c) {
        print_color("Heap alloc FAILED\n", COLOR_RED_ON_BLACK);
        return;
    }

    /*
     * 실제로 써봐서 page fault가 안 나는지 확인한다.
     */
    ((u64*)a)[0] = 0x1111222233334444ULL;
    ((u64*)b)[0] = 0x5555666677778888ULL;
    ((u64*)c)[0] = 0x9999AAAABBBBCCCCULL;

    print("A value = ");
    print_hex64(((u64*)a)[0]);
    print("\n");

    print("B value = ");
    print_hex64(((u64*)b)[0]);
    print("\n");

    print("C value = ");
    print_hex64(((u64*)c)[0]);
    print("\n");

    print("free after allocs = ");
    print_dec64(heap_total_free_bytes());
    print(" bytes\n");

    kfree(b);

    print("freed B\n");

    void* d = kmalloc(64);

    print("alloc D 64    = ");
    print_hex64((u64)d);
    print("\n");

    if (d == b) {
        print_color("reuse freed block OK\n", COLOR_GREEN_ON_BLACK);
    } else {
        print_color("reuse freed block not exact, but allocator still valid\n", COLOR_YELLOW_ON_BLACK);
    }

    /*
     * heap 확장 테스트.
     * 초기 heap 16KB보다 큰 요청을 넣어서 새 page mapping이 되는지 확인한다.
     */
    void* e = kmalloc(20000);

    print("alloc E 20000 = ");
    print_hex64((u64)e);
    print("\n");

    if (!e) {
        print_color("Heap expansion FAILED\n", COLOR_RED_ON_BLACK);
        return;
    }

    ((u8*)e)[0] = 0xAB;
    ((u8*)e)[19999] = 0xCD;

    print("E first byte  = ");
    print_hex64(((u8*)e)[0]);
    print("\n");

    print("E last byte   = ");
    print_hex64(((u8*)e)[19999]);
    print("\n");

    print("heap mapped end = ");
    print_hex64(heap_mapped_end);
    print("\n");

    print("block count = ");
    print_dec64(heap_block_count());
    print("\n");

    print("largest free = ");
    print_dec64(heap_largest_free_block());
    print(" bytes\n");

    kfree(a);
    kfree(c);
    kfree(d);
    kfree(e);

    print("free after cleanup = ");
    print_dec64(heap_total_free_bytes());
    print(" bytes\n");

    print("largest free after cleanup = ");
    print_dec64(heap_largest_free_block());
    print(" bytes\n");

    print_color("Heap OK\n", COLOR_GREEN_ON_BLACK);
}