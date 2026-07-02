#include "console.h"
#include "heap.h"
#include "print.h"
#include "ramfs.h"
#include "types.h"
#include "vfs.h"

static vfs_node_t* root_node = 0;
static u32 next_node_id = 1;

static u64 ramfs_strlen(const char* s) {
    u64 len = 0;

    if (!s) {
        return 0;
    }

    while (s[len]) {
        len++;
    }

    return len;
}

static void ramfs_memcpy(void* dst, const void* src, u64 size) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;

    for (u64 i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

static void ramfs_memzero(void* dst, u64 size) {
    u8* d = (u8*)dst;

    for (u64 i = 0; i < size; i++) {
        d[i] = 0;
    }
}

static void ramfs_copy_name(char* dst, const char* src) {
    u64 i = 0;

    if (!dst) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (src[i] && i + 1 < VFS_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static u64 ramfs_read(
    vfs_node_t* node,
    u64 offset,
    void* buffer,
    u64 size
) {
    if (!node || !buffer || size == 0) {
        return 0;
    }

    if (offset >= node->size) {
        return 0;
    }

    u64 available = node->size - offset;
    u64 to_read = size;

    if (to_read > available) {
        to_read = available;
    }

    ramfs_memcpy(buffer, node->data + offset, to_read);

    return to_read;
}

static u64 ramfs_write(
    vfs_node_t* node,
    u64 offset,
    const void* buffer,
    u64 size
) {
    if (!node || !buffer || size == 0) {
        return 0;
    }

    u64 end = offset + size;

    if (end > node->capacity) {
        u64 new_capacity = end;

        if (new_capacity < 64) {
            new_capacity = 64;
        }

        u8* new_data = (u8*)kmalloc(new_capacity);

        if (!new_data) {
            return 0;
        }

        ramfs_memzero(new_data, new_capacity);

        if (node->data && node->size > 0) {
            ramfs_memcpy(new_data, node->data, node->size);
            kfree(node->data);
        }

        node->data = new_data;
        node->capacity = new_capacity;
    }

    ramfs_memcpy(node->data + offset, buffer, size);

    if (end > node->size) {
        node->size = end;
    }

    return size;
}

static vfs_node_t* ramfs_alloc_node(
    const char* name,
    vfs_node_type_t type
) {
    vfs_node_t* node = (vfs_node_t*)kzalloc(sizeof(vfs_node_t));

    if (!node) {
        return 0;
    }

    node->id = next_node_id;
    next_node_id++;

    ramfs_copy_name(node->name, name);

    node->type = type;

    node->parent = 0;
    node->first_child = 0;
    node->next_sibling = 0;

    node->data = 0;
    node->size = 0;
    node->capacity = 0;

    if (type == VFS_NODE_FILE) {
        node->read = ramfs_read;
        node->write = ramfs_write;
    } else {
        node->read = 0;
        node->write = 0;
    }

    return node;
}

static void ramfs_add_child(vfs_node_t* parent, vfs_node_t* child) {
    if (!parent || !child) {
        return;
    }

    child->parent = parent;

    if (!parent->first_child) {
        parent->first_child = child;
        return;
    }

    vfs_node_t* current = parent->first_child;

    while (current->next_sibling) {
        current = current->next_sibling;
    }

    current->next_sibling = child;
}

vfs_node_t* ramfs_root(void) {
    return root_node;
}

vfs_node_t* ramfs_create_dir(vfs_node_t* parent, const char* name) {
    vfs_node_t* node = ramfs_alloc_node(name, VFS_NODE_DIR);

    if (!node) {
        return 0;
    }

    if (parent) {
        ramfs_add_child(parent, node);
    }

    return node;
}

vfs_node_t* ramfs_create_file(
    vfs_node_t* parent,
    const char* name,
    const char* content
) {
    vfs_node_t* node = ramfs_alloc_node(name, VFS_NODE_FILE);

    if (!node) {
        return 0;
    }

    if (content) {
        u64 len = ramfs_strlen(content);

        if (len > 0) {
            u8* data = (u8*)kmalloc(len);

            if (!data) {
                return 0;
            }

            ramfs_memcpy(data, content, len);

            node->data = data;
            node->size = len;
            node->capacity = len;
        }
    }

    if (parent) {
        ramfs_add_child(parent, node);
    }

    return node;
}

void ramfs_init(void) {
    next_node_id = 1;

    root_node = ramfs_create_dir(0, "");

    if (!root_node) {
        print_color("RAMFS init failed\n", COLOR_RED_ON_BLACK);
        return;
    }

    vfs_node_t* bin = ramfs_create_dir(root_node, "bin");
    vfs_node_t* dev = ramfs_create_dir(root_node, "dev");
    vfs_node_t* etc = ramfs_create_dir(root_node, "etc");
    vfs_node_t* tmp = ramfs_create_dir(root_node, "tmp");

    (void)bin;
    (void)dev;
    (void)tmp;

    ramfs_create_file(
        root_node,
        "hello.txt",
        "hello from my-os ramfs\n"
    );

    ramfs_create_file(
        etc,
        "motd",
        "welcome to my-os\n"
    );

    vfs_init(root_node);

    print_color("RAMFS initialized\n", COLOR_GREEN_ON_BLACK);
}
