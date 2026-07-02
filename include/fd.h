#ifndef MY_OS_FD_H
#define MY_OS_FD_H

#include "types.h"

#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2

#define FD_INVALID -1

typedef enum {
    FD_TYPE_UNUSED = 0,
    FD_TYPE_CONSOLE,
    FD_TYPE_VFS_NODE
} fd_type_t;

typedef struct fd_entry {
    u32 used;
    fd_type_t type;

    void* object;
    u64 offset;

    u32 readable;
    u32 writable;
} fd_entry_t;

void fd_init(void);

s32 fd_open(const char* path);
s64 fd_read(s32 fd, void* buffer, u64 size);
s64 fd_write(s32 fd, const void* buffer, u64 size);
s32 fd_close(s32 fd);

u32 fd_count_used(void);
void fd_print_table(void);

void fd_register_builtin_commands(void);

#endif
