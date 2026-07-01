#include "console.h"
#include "multiboot2.h"
#include "print.h"
#include "utils.h"

void mb2_dump_info(u64 mb2_info_addr) {
    mb2_info_header_t* info = (mb2_info_header_t*)mb2_info_addr;

    print_color("[Multiboot2 info]\n", COLOR_CYAN_ON_BLACK);

    print("total_size = ");
    print_dec64(info->total_size);
    print(" bytes\n");

    print("reserved   = ");
    print_hex32(info->reserved);
    print("\n\n");

    u64 info_end = mb2_info_addr + info->total_size;
    u64 tag_addr = mb2_info_addr + sizeof(mb2_info_header_t);

    while (tag_addr + sizeof(mb2_tag_t) <= info_end) {
        mb2_tag_t* tag = (mb2_tag_t*)tag_addr;

        if (tag->type == MB2_TAG_TYPE_END) {
            print_color("end tag reached\n", COLOR_CYAN_ON_BLACK);
            return;
        }

        if (tag->size < sizeof(mb2_tag_t)) {
            print_color("invalid tag size\n", COLOR_RED_ON_BLACK);
            return;
        }

        if (tag_addr + tag->size > info_end) {
            print_color("tag exceeds multiboot info size\n", COLOR_RED_ON_BLACK);
            return;
        }

        print("tag type=");
        print_dec64(tag->type);

        print(" size=");
        print_dec64(tag->size);

        print("\n");

        if (tag->type == MB2_TAG_TYPE_CMDLINE) {
            mb2_tag_string_t* cmdline = (mb2_tag_string_t*)tag;

            print("  cmdline     = ");
            print(cmdline->string);
            print("\n");
        } else if (tag->type == MB2_TAG_TYPE_BOOT_LOADER) {
            mb2_tag_string_t* bootloader = (mb2_tag_string_t*)tag;

            print("  bootloader  = ");
            print(bootloader->string);
            print("\n");
        } else if (tag->type == MB2_TAG_TYPE_IMAGE_LOAD_BASE) {
            u32* load_base = (u32*)(tag_addr + sizeof(mb2_tag_t));

            print("  image load base = ");
            print_hex32(*load_base);
            print("\n");
        } else if (tag->type == MB2_TAG_TYPE_MMAP) {
            mb2_tag_mmap_t* mmap = (mb2_tag_mmap_t*)tag;

            print_color("  memory map:\n", COLOR_CYAN_ON_BLACK);

            print("    entry_size    = ");
            print_dec64(mmap->entry_size);
            print("\n");

            print("    entry_version = ");
            print_dec64(mmap->entry_version);
            print("\n");

            u64 entries_start = (u64)&mmap->entries[0];
            u64 entries_end = (u64)mmap + mmap->size;

            for (u64 entry_addr = entries_start;
                 entry_addr + sizeof(mb2_mmap_entry_t) <= entries_end;
                 entry_addr += mmap->entry_size) {
                mb2_mmap_entry_t* entry = (mb2_mmap_entry_t*)entry_addr;

                print("    addr=");
                print_hex64(entry->addr);

                print(" len=");
                print_hex64(entry->len);

                print(" type=");
                print_dec64(entry->type);

                print(" ");
                print(mb2_mmap_type_name(entry->type));

                print("\n");
            }
        }

        u64 next_tag_addr = align_up_u64(tag_addr + tag->size, 8);

        if (next_tag_addr <= tag_addr) {
            print_color("tag parser stuck\n", COLOR_RED_ON_BLACK);
            return;
        }

        tag_addr = next_tag_addr;
    }

    print_color("end of multiboot info reached without end tag\n", COLOR_RED_ON_BLACK);
}

mb2_tag_mmap_t* mb2_find_mmap_tag(u64 mb2_info_addr) {
    mb2_info_header_t* info = (mb2_info_header_t*)mb2_info_addr;

    u64 info_end = mb2_info_addr + info->total_size;
    u64 tag_addr = mb2_info_addr + sizeof(mb2_info_header_t);

    while (tag_addr + sizeof(mb2_tag_t) <= info_end) {
        mb2_tag_t* tag = (mb2_tag_t*)tag_addr;

        if (tag->type == MB2_TAG_TYPE_END) {
            return 0;
        }

        if (tag->size < sizeof(mb2_tag_t)) {
            return 0;
        }

        if (tag_addr + tag->size > info_end) {
            return 0;
        }

        if (tag->type == MB2_TAG_TYPE_MMAP) {
            return (mb2_tag_mmap_t*)tag;
        }

        tag_addr = align_up_u64(tag_addr + tag->size, 8);
    }

    return 0;
}

const char* mb2_mmap_type_name(u32 type) {
    if (type == 1) {
        return "available";
    }

    if (type == 2) {
        return "reserved";
    }

    if (type == 3) {
        return "acpi_reclaimable";
    }

    if (type == 4) {
        return "nvs";
    }

    if (type == 5) {
        return "badram";
    }

    return "unknown";
}
