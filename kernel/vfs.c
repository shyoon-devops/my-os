#include "command.h"
#include "console.h"
#include "print.h"
#include "types.h"
#include "vfs.h"

#define VFS_CMD_PATH_MAX 128

static vfs_node_t* root_node = 0;

static u32 str_equal(const char* a, const char* b) {
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

static u32 is_space(char c) {
    return c == ' ' || c == '\t';
}

static u32 copy_first_arg(const char* args, char* out, u64 out_size) {
    if (!out || out_size == 0) {
        return 0;
    }

    out[0] = '\0';

    if (!args) {
        return 0;
    }

    while (*args && is_space(*args)) {
        args++;
    }

    if (*args == '\0') {
        return 0;
    }

    u64 index = 0;

    while (*args && !is_space(*args)) {
        if (index + 1 >= out_size) {
            break;
        }

        out[index] = *args;
        index++;
        args++;
    }

    out[index] = '\0';

    return index > 0;
}

static u32 path_is_separator(char c) {
    return c == '/';
}

void vfs_init(vfs_node_t* root) {
    root_node = root;

    print_color("VFS initialized\n", COLOR_GREEN_ON_BLACK);
}

vfs_node_t* vfs_root(void) {
    return root_node;
}

vfs_node_t* vfs_find_child(vfs_node_t* dir, const char* name) {
    if (!dir || !name) {
        return 0;
    }

    if (dir->type != VFS_NODE_DIR) {
        return 0;
    }

    vfs_node_t* current = dir->first_child;

    while (current) {
        if (str_equal(current->name, name)) {
            return current;
        }

        current = current->next_sibling;
    }

    return 0;
}

vfs_node_t* vfs_lookup(const char* path) {
    if (!root_node || !path) {
        return 0;
    }

    if (path[0] != '/') {
        return 0;
    }

    vfs_node_t* current = root_node;
    u64 index = 0;

    while (path[index]) {
        while (path_is_separator(path[index])) {
            index++;
        }

        if (path[index] == '\0') {
            break;
        }

        char component[VFS_NAME_MAX];
        u64 component_len = 0;

        while (path[index] && !path_is_separator(path[index])) {
            if (component_len + 1 >= VFS_NAME_MAX) {
                return 0;
            }

            component[component_len] = path[index];
            component_len++;
            index++;
        }

        component[component_len] = '\0';

        current = vfs_find_child(current, component);

        if (!current) {
            return 0;
        }
    }

    return current;
}

u64 vfs_read(vfs_node_t* node, u64 offset, void* buffer, u64 size) {
    if (!node || !buffer || size == 0) {
        return 0;
    }

    if (node->type != VFS_NODE_FILE && node->type != VFS_NODE_DEVICE) {
        return 0;
    }

    if (!node->read) {
        return 0;
    }

    return node->read(node, offset, buffer, size);
}

u64 vfs_write(vfs_node_t* node, u64 offset, const void* buffer, u64 size) {
    if (!node || !buffer || size == 0) {
        return 0;
    }

    if (node->type != VFS_NODE_FILE && node->type != VFS_NODE_DEVICE) {
        return 0;
    }

    if (!node->write) {
        return 0;
    }

    return node->write(node, offset, buffer, size);
}

static u32 count_nodes_recursive(vfs_node_t* node) {
    if (!node) {
        return 0;
    }

    u32 count = 1;

    vfs_node_t* child = node->first_child;

    while (child) {
        count += count_nodes_recursive(child);
        child = child->next_sibling;
    }

    return count;
}

u32 vfs_count_nodes(void) {
    return count_nodes_recursive(root_node);
}

void vfs_list_dir(vfs_node_t* dir) {
    if (!dir) {
        print_color("ls: null node\n", COLOR_RED_ON_BLACK);
        return;
    }

    if (dir->type != VFS_NODE_DIR) {
        print_color("ls: not a directory\n", COLOR_RED_ON_BLACK);
        return;
    }

    vfs_node_t* child = dir->first_child;

    while (child) {
        if (child->type == VFS_NODE_DIR) {
            print("d ");
        } else if (child->type == VFS_NODE_FILE) {
            print("f ");
        } else if (child->type == VFS_NODE_DEVICE) {
            print("c ");
        } else {
            print("? ");
        }

        print(child->name);

        if (child->type == VFS_NODE_FILE) {
            print("  ");
            print_dec64(child->size);
            print(" bytes");
        }

        print("\n");

        child = child->next_sibling;
    }
}

const char* vfs_node_type_name(vfs_node_type_t type) {
    switch (type) {
        case VFS_NODE_FILE:
            return "file";
        case VFS_NODE_DIR:
            return "dir";
        case VFS_NODE_DEVICE:
            return "device";
        case VFS_NODE_UNKNOWN:
            return "unknown";
        default:
            return "invalid";
    }
}

static void cmd_ls(const char* args) {
    char path[VFS_CMD_PATH_MAX];

    if (!copy_first_arg(args, path, sizeof(path))) {
        path[0] = '/';
        path[1] = '\0';
    }

    vfs_node_t* node = vfs_lookup(path);

    if (!node) {
        print("ls: not found: ");
        print(path);
        print("\n");
        return;
    }

    if (node->type == VFS_NODE_DIR) {
        vfs_list_dir(node);
        return;
    }

    print(node->name);
    print("  ");
    print(vfs_node_type_name(node->type));

    if (node->type == VFS_NODE_FILE) {
        print("  ");
        print_dec64(node->size);
        print(" bytes");
    }

    print("\n");
}

static void cmd_cat(const char* args) {
    char path[VFS_CMD_PATH_MAX];

    if (!copy_first_arg(args, path, sizeof(path))) {
        print("usage: cat <path>\n");
        return;
    }

    vfs_node_t* node = vfs_lookup(path);

    if (!node) {
        print("cat: not found: ");
        print(path);
        print("\n");
        return;
    }

    if (node->type != VFS_NODE_FILE) {
        print("cat: not a file: ");
        print(path);
        print("\n");
        return;
    }

    char buffer[129];
    u64 offset = 0;

    for (;;) {
        u64 read = vfs_read(node, offset, buffer, sizeof(buffer) - 1);

        if (read == 0) {
            break;
        }

        buffer[read] = '\0';
        print(buffer);

        offset += read;
    }

    if (node->size > 0) {
        u8 last = 0;
        vfs_read(node, node->size - 1, &last, 1);

        if (last != '\n') {
            print("\n");
        }
    }
}

static void cmd_fsinfo(const char* args) {
    (void)args;

    print("vfs root = ");
    print_hex64((u64)vfs_root());
    print("\n");

    print("vfs nodes = ");
    print_dec64(vfs_count_nodes());
    print("\n");
}

void vfs_register_builtin_commands(void) {
    command_register("ls", "list VFS directory", cmd_ls);
    command_register("cat", "print VFS file", cmd_cat);
    command_register("fsinfo", "show VFS info", cmd_fsinfo);
}
