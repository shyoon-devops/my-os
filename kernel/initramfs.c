#include "command.h"
#include "console.h"
#include "initramfs.h"
#include "print.h"
#include "ramfs.h"
#include "types.h"
#include "vfs.h"

#define TAR_BLOCK_SIZE 512
#define TAR_NAME_SIZE 100
#define TAR_PREFIX_SIZE 155
#define INITRAMFS_PATH_MAX 160

extern const u8 _binary_build_initramfs_tar_start[];
extern const u8 _binary_build_initramfs_tar_end[];

static u32 loaded_files = 0;
static u32 loaded_dirs = 0;
static u64 loaded_bytes = 0;
static u64 archive_bytes = 0;

static u32 str_eq(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }

    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }

        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static u32 str_len(const char* s) {
    u32 len = 0;

    if (!s) {
        return 0;
    }

    while (s[len]) {
        len++;
    }

    return len;
}

static void str_copy_limit(char* dst, const char* src, u32 dst_size) {
    if (!dst || dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    u32 i = 0;

    while (src[i] && i + 1 < dst_size) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static u32 append_char(char* dst, u32* index, u32 dst_size, char c) {
    if (!dst || !index || dst_size == 0) {
        return 0;
    }

    if (*index + 1 >= dst_size) {
        return 0;
    }

    dst[*index] = c;
    *index = *index + 1;
    dst[*index] = '\0';

    return 1;
}

static u32 append_field(
    char* dst,
    u32* index,
    u32 dst_size,
    const char* field,
    u32 field_size
) {
    if (!dst || !index || !field || dst_size == 0) {
        return 0;
    }

    for (u32 i = 0; i < field_size; i++) {
        if (field[i] == '\0') {
            break;
        }

        if (!append_char(dst, index, dst_size, field[i])) {
            return 0;
        }
    }

    return 1;
}

static u32 tar_block_is_zero(const u8* block) {
    for (u32 i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (block[i] != 0) {
            return 0;
        }
    }

    return 1;
}

static u64 tar_parse_octal(const char* field, u32 field_size) {
    u64 value = 0;

    for (u32 i = 0; i < field_size; i++) {
        char c = field[i];

        if (c == '\0' || c == ' ') {
            break;
        }

        if (c < '0' || c > '7') {
            continue;
        }

        value = value * 8 + (u64)(c - '0');
    }

    return value;
}

static u64 align_tar_size(u64 size) {
    return ((size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE) * TAR_BLOCK_SIZE;
}

static u32 tar_get_raw_path(const u8* header, char* out, u32 out_size) {
    if (!header || !out || out_size == 0) {
        return 0;
    }

    out[0] = '\0';

    const char* name = (const char*)header;
    const char* prefix = (const char*)(header + 345);

    u32 index = 0;

    if (prefix[0]) {
        if (!append_field(out, &index, out_size, prefix, TAR_PREFIX_SIZE)) {
            return 0;
        }

        if (!append_char(out, &index, out_size, '/')) {
            return 0;
        }
    }

    if (!append_field(out, &index, out_size, name, TAR_NAME_SIZE)) {
        return 0;
    }

    return index > 0;
}

static u32 normalize_path(const char* raw, char* out, u32 out_size) {
    if (!raw || !out || out_size == 0) {
        return 0;
    }

    out[0] = '\0';

    const char* p = raw;

    while (p[0] == '/') {
        p++;
    }

    if (p[0] == '.' && p[1] == '/') {
        p += 2;
    }

    if (p[0] == '.' && p[1] == '\0') {
        return 0;
    }

    str_copy_limit(out, p, out_size);

    u32 len = str_len(out);

    while (len > 0 && out[len - 1] == '/') {
        out[len - 1] = '\0';
        len--;
    }

    return len > 0;
}

static vfs_node_t* find_child(vfs_node_t* parent, const char* name) {
    if (!parent || !name) {
        return 0;
    }

    vfs_node_t* child = parent->first_child;

    while (child) {
        if (str_eq(child->name, name)) {
            return child;
        }

        child = child->next_sibling;
    }

    return 0;
}

static void copy_component(
    char* out,
    u32 out_size,
    const char* path,
    u32 start,
    u32 end
) {
    if (!out || out_size == 0 || !path || end < start) {
        return;
    }

    u32 index = 0;

    for (u32 i = start; i < end; i++) {
        if (index + 1 >= out_size) {
            break;
        }

        out[index] = path[i];
        index++;
    }

    out[index] = '\0';
}

static vfs_node_t* ensure_dir_path(const char* path) {
    vfs_node_t* current = ramfs_root();

    if (!current) {
        return 0;
    }

    if (!path || path[0] == '\0') {
        return current;
    }

    u32 len = str_len(path);
    u32 start = 0;

    while (start < len) {
        while (start < len && path[start] == '/') {
            start++;
        }

        if (start >= len) {
            break;
        }

        u32 end = start;

        while (end < len && path[end] != '/') {
            end++;
        }

        char name[VFS_NAME_MAX];
        copy_component(name, sizeof(name), path, start, end);

        vfs_node_t* child = find_child(current, name);

        if (!child) {
            child = ramfs_create_dir(current, name);
            loaded_dirs++;
        }

        if (!child || child->type != VFS_NODE_DIR) {
            return 0;
        }

        current = child;
        start = end + 1;
    }

    return current;
}

static vfs_node_t* ensure_parent_dir_for_file(
    const char* path,
    char* basename,
    u32 basename_size
) {
    if (!path || !basename || basename_size == 0) {
        return 0;
    }

    basename[0] = '\0';

    u32 len = str_len(path);

    if (len == 0) {
        return 0;
    }

    u32 last_slash = 0;
    u32 has_slash = 0;

    for (u32 i = 0; i < len; i++) {
        if (path[i] == '/') {
            last_slash = i;
            has_slash = 1;
        }
    }

    if (!has_slash) {
        str_copy_limit(basename, path, basename_size);
        return ramfs_root();
    }

    copy_component(basename, basename_size, path, last_slash + 1, len);

    char parent_path[INITRAMFS_PATH_MAX];
    copy_component(parent_path, sizeof(parent_path), path, 0, last_slash);

    return ensure_dir_path(parent_path);
}

static void load_dir_entry(const char* path) {
    if (!path || path[0] == '\0') {
        return;
    }

    ensure_dir_path(path);
}

static void load_file_entry(const char* path, const void* data, u64 size) {
    if (!path || path[0] == '\0') {
        return;
    }

    char basename[VFS_NAME_MAX];
    vfs_node_t* parent = ensure_parent_dir_for_file(
        path,
        basename,
        sizeof(basename)
    );

    if (!parent || basename[0] == '\0') {
        return;
    }

    vfs_node_t* existing = find_child(parent, basename);

    if (existing) {
        return;
    }

    if (ramfs_create_file_from_buffer(parent, basename, data, size)) {
        loaded_files++;
        loaded_bytes += size;
    }
}

void initramfs_load(void) {
    loaded_files = 0;
    loaded_dirs = 0;
    loaded_bytes = 0;

    const u8* archive = _binary_build_initramfs_tar_start;
    archive_bytes = (u64)(_binary_build_initramfs_tar_end - _binary_build_initramfs_tar_start);

    u64 offset = 0;

    while (offset + TAR_BLOCK_SIZE <= archive_bytes) {
        const u8* header = archive + offset;

        if (tar_block_is_zero(header)) {
            break;
        }

        char raw_path[INITRAMFS_PATH_MAX];
        char path[INITRAMFS_PATH_MAX];

        if (!tar_get_raw_path(header, raw_path, sizeof(raw_path))) {
            offset += TAR_BLOCK_SIZE;
            continue;
        }

        u64 file_size = tar_parse_octal((const char*)(header + 124), 12);
        char typeflag = (char)header[156];

        if (normalize_path(raw_path, path, sizeof(path))) {
            const void* file_data = archive + offset + TAR_BLOCK_SIZE;

            if (typeflag == '5') {
                load_dir_entry(path);
            } else if (typeflag == '\0' || typeflag == '0') {
                load_file_entry(path, file_data, file_size);
            }
        }

        offset += TAR_BLOCK_SIZE + align_tar_size(file_size);
    }

    print_color("initramfs loaded\n", COLOR_GREEN_ON_BLACK);
    print("initramfs files = ");
    print_dec64(loaded_files);
    print(", dirs = ");
    print_dec64(loaded_dirs);
    print(", bytes = ");
    print_dec64(loaded_bytes);
    print("\n");
}

static void cmd_initramfsinfo(const char* args) {
    (void)args;

    print("initramfs archive bytes = ");
    print_dec64(archive_bytes);
    print("\n");

    print("loaded files = ");
    print_dec64(loaded_files);
    print("\n");

    print("loaded dirs = ");
    print_dec64(loaded_dirs);
    print("\n");

    print("loaded file bytes = ");
    print_dec64(loaded_bytes);
    print("\n");
}

void initramfs_register_builtin_commands(void) {
    command_register("initramfsinfo", "show initramfs load stats", cmd_initramfsinfo);
}
