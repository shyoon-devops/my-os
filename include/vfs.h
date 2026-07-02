#ifndef MY_OS_VFS_H
#define MY_OS_VFS_H

#include "types.h"

#define VFS_NAME_MAX 32

typedef enum {
    VFS_NODE_UNKNOWN = 0,
    VFS_NODE_FILE,
    VFS_NODE_DIR,
    VFS_NODE_DEVICE
} vfs_node_type_t;

struct vfs_node;

typedef u64 (*vfs_read_fn)(
    struct vfs_node* node,
    u64 offset,
    void* buffer,
    u64 size
);

typedef u64 (*vfs_write_fn)(
    struct vfs_node* node,
    u64 offset,
    const void* buffer,
    u64 size
);

typedef struct vfs_node {
    u32 id;

    char name[VFS_NAME_MAX];
    vfs_node_type_t type;

    struct vfs_node* parent;
    struct vfs_node* first_child;
    struct vfs_node* next_sibling;

    u8* data;
    u64 size;
    u64 capacity;

    vfs_read_fn read;
    vfs_write_fn write;
} vfs_node_t;

void vfs_init(vfs_node_t* root);
vfs_node_t* vfs_root(void);

vfs_node_t* vfs_lookup(const char* path);
vfs_node_t* vfs_find_child(vfs_node_t* dir, const char* name);

u64 vfs_read(vfs_node_t* node, u64 offset, void* buffer, u64 size);
u64 vfs_write(vfs_node_t* node, u64 offset, const void* buffer, u64 size);

u32 vfs_count_nodes(void);
void vfs_list_dir(vfs_node_t* dir);

const char* vfs_node_type_name(vfs_node_type_t type);

void vfs_register_builtin_commands(void);

#endif
