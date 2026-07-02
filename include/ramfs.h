#ifndef MY_OS_RAMFS_H
#define MY_OS_RAMFS_H

#include "types.h"
#include "vfs.h"

void ramfs_init(void);

vfs_node_t* ramfs_root(void);

vfs_node_t* ramfs_create_dir(vfs_node_t* parent, const char* name);
vfs_node_t* ramfs_create_file(
    vfs_node_t* parent,
    const char* name,
    const char* content
);

#endif
