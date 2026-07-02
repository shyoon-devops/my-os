#include "command.h"
#include "console.h"
#include "fd.h"
#include "print.h"
#include "spinlock.h"
#include "types.h"
#include "vfs.h"

#define FD_TABLE_SIZE 32
#define FD_CMD_PATH_MAX 128

static fd_entry_t fd_table[FD_TABLE_SIZE];
static spinlock_t fd_lock;

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

static void fd_clear_entry(u32 index) {
    fd_table[index].used = 0;
    fd_table[index].type = FD_TYPE_UNUSED;
    fd_table[index].object = 0;
    fd_table[index].offset = 0;
    fd_table[index].readable = 0;
    fd_table[index].writable = 0;
}

static const char* fd_type_name(fd_type_t type) {
    switch (type) {
        case FD_TYPE_UNUSED:
            return "unused";
        case FD_TYPE_CONSOLE:
            return "console";
        case FD_TYPE_VFS_NODE:
            return "vfs";
        default:
            return "unknown";
    }
}

void fd_init(void) {
    spinlock_init(&fd_lock);

    for (u32 i = 0; i < FD_TABLE_SIZE; i++) {
        fd_clear_entry(i);
    }

    /*
     * fd 0/1/2는 표준 입출력 예약 번호다.
     *
     * 아직 /dev/tty0가 VFS device node로 붙지 않았으므로,
     * stdin은 읽기 불가 예약 상태로 두고,
     * stdout/stderr는 console 출력으로 연결한다.
     */
    fd_table[FD_STDIN].used = 1;
    fd_table[FD_STDIN].type = FD_TYPE_CONSOLE;
    fd_table[FD_STDIN].object = 0;
    fd_table[FD_STDIN].offset = 0;
    fd_table[FD_STDIN].readable = 0;
    fd_table[FD_STDIN].writable = 0;

    fd_table[FD_STDOUT].used = 1;
    fd_table[FD_STDOUT].type = FD_TYPE_CONSOLE;
    fd_table[FD_STDOUT].object = 0;
    fd_table[FD_STDOUT].offset = 0;
    fd_table[FD_STDOUT].readable = 0;
    fd_table[FD_STDOUT].writable = 1;

    fd_table[FD_STDERR].used = 1;
    fd_table[FD_STDERR].type = FD_TYPE_CONSOLE;
    fd_table[FD_STDERR].object = 0;
    fd_table[FD_STDERR].offset = 0;
    fd_table[FD_STDERR].readable = 0;
    fd_table[FD_STDERR].writable = 1;

    print_color("FD table initialized\n", COLOR_GREEN_ON_BLACK);
}

static s32 fd_alloc_locked(void) {
    /*
     * 0, 1, 2는 stdin/stdout/stderr로 예약.
     * 일반 open은 3번부터 할당한다.
     */
    for (u32 i = 3; i < FD_TABLE_SIZE; i++) {
        if (!fd_table[i].used) {
            fd_table[i].used = 1;
            return (s32)i;
        }
    }

    return FD_INVALID;
}

s32 fd_open(const char* path) {
    if (!path) {
        return FD_INVALID;
    }

    vfs_node_t* node = vfs_lookup(path);

    if (!node) {
        return FD_INVALID;
    }

    if (node->type != VFS_NODE_FILE && node->type != VFS_NODE_DEVICE) {
        return FD_INVALID;
    }

    u64 flags;
    s32 fd;

    spin_lock_irqsave(&fd_lock, &flags);

    fd = fd_alloc_locked();

    if (fd != FD_INVALID) {
        fd_table[fd].type = FD_TYPE_VFS_NODE;
        fd_table[fd].object = node;
        fd_table[fd].offset = 0;
        fd_table[fd].readable = 1;
        fd_table[fd].writable = 1;
    }

    spin_unlock_irqrestore(&fd_lock, flags);

    return fd;
}

s64 fd_read(s32 fd, void* buffer, u64 size) {
    if (fd < 0 || fd >= FD_TABLE_SIZE || !buffer || size == 0) {
        return -1;
    }

    u64 flags;
    fd_entry_t entry;

    spin_lock_irqsave(&fd_lock, &flags);

    if (!fd_table[fd].used || !fd_table[fd].readable) {
        spin_unlock_irqrestore(&fd_lock, flags);
        return -1;
    }

    entry = fd_table[fd];

    spin_unlock_irqrestore(&fd_lock, flags);

    if (entry.type == FD_TYPE_VFS_NODE) {
        vfs_node_t* node = (vfs_node_t*)entry.object;

        u64 read = vfs_read(node, entry.offset, buffer, size);

        spin_lock_irqsave(&fd_lock, &flags);

        if (fd_table[fd].used && fd_table[fd].object == entry.object) {
            fd_table[fd].offset += read;
        }

        spin_unlock_irqrestore(&fd_lock, flags);

        return (s64)read;
    }

    return -1;
}

s64 fd_write(s32 fd, const void* buffer, u64 size) {
    if (fd < 0 || fd >= FD_TABLE_SIZE || !buffer || size == 0) {
        return -1;
    }

    u64 flags;
    fd_entry_t entry;

    spin_lock_irqsave(&fd_lock, &flags);

    if (!fd_table[fd].used || !fd_table[fd].writable) {
        spin_unlock_irqrestore(&fd_lock, flags);
        return -1;
    }

    entry = fd_table[fd];

    spin_unlock_irqrestore(&fd_lock, flags);

    if (entry.type == FD_TYPE_CONSOLE) {
        const char* text = (const char*)buffer;

        for (u64 i = 0; i < size; i++) {
            console_put_char(text[i]);
        }

        return (s64)size;
    }

    if (entry.type == FD_TYPE_VFS_NODE) {
        vfs_node_t* node = (vfs_node_t*)entry.object;

        u64 written = vfs_write(node, entry.offset, buffer, size);

        spin_lock_irqsave(&fd_lock, &flags);

        if (fd_table[fd].used && fd_table[fd].object == entry.object) {
            fd_table[fd].offset += written;
        }

        spin_unlock_irqrestore(&fd_lock, flags);

        return (s64)written;
    }

    return -1;
}

s32 fd_close(s32 fd) {
    if (fd < 0 || fd >= FD_TABLE_SIZE) {
        return -1;
    }

    /*
     * stdio는 닫지 않는다.
     */
    if (fd == FD_STDIN || fd == FD_STDOUT || fd == FD_STDERR) {
        return -1;
    }

    u64 flags;

    spin_lock_irqsave(&fd_lock, &flags);

    if (!fd_table[fd].used) {
        spin_unlock_irqrestore(&fd_lock, flags);
        return -1;
    }

    fd_clear_entry((u32)fd);

    spin_unlock_irqrestore(&fd_lock, flags);

    return 0;
}

u32 fd_count_used(void) {
    u64 flags;
    u32 count = 0;

    spin_lock_irqsave(&fd_lock, &flags);

    for (u32 i = 0; i < FD_TABLE_SIZE; i++) {
        if (fd_table[i].used) {
            count++;
        }
    }

    spin_unlock_irqrestore(&fd_lock, flags);

    return count;
}

void fd_print_table(void) {
    u64 flags;

    spin_lock_irqsave(&fd_lock, &flags);

    print("fd table:\n");

    for (u32 i = 0; i < FD_TABLE_SIZE; i++) {
        if (!fd_table[i].used) {
            continue;
        }

        print("  ");
        print_dec64(i);
        print(" type=");
        print(fd_type_name(fd_table[i].type));

        print(" readable=");
        print_dec64(fd_table[i].readable);

        print(" writable=");
        print_dec64(fd_table[i].writable);

        print(" offset=");
        print_dec64(fd_table[i].offset);

        if (fd_table[i].type == FD_TYPE_VFS_NODE && fd_table[i].object) {
            vfs_node_t* node = (vfs_node_t*)fd_table[i].object;

            print(" node=");
            print(node->name);

            print(" size=");
            print_dec64(node->size);
        }

        print("\n");
    }

    spin_unlock_irqrestore(&fd_lock, flags);
}

static void cmd_fdinfo(const char* args) {
    (void)args;

    print("used fds = ");
    print_dec64(fd_count_used());
    print("\n");

    fd_print_table();
}

static void cmd_fdcat(const char* args) {
    char path[FD_CMD_PATH_MAX];

    if (!copy_first_arg(args, path, sizeof(path))) {
        print("usage: fdcat <path>\n");
        return;
    }

    s32 fd = fd_open(path);

    if (fd < 0) {
        print("fdcat: open failed: ");
        print(path);
        print("\n");
        return;
    }

    char buffer[129];

    for (;;) {
        s64 read = fd_read(fd, buffer, sizeof(buffer) - 1);

        if (read < 0) {
            print("fdcat: read failed\n");
            break;
        }

        if (read == 0) {
            break;
        }

        buffer[read] = '\0';
        fd_write(FD_STDOUT, buffer, (u64)read);
    }

    fd_close(fd);
}

static void cmd_fdwrite(const char* args) {
    if (!args) {
        print("usage: fdwrite <text>\n");
        return;
    }

    while (*args && is_space(*args)) {
        args++;
    }

    u64 len = 0;

    while (args[len]) {
        len++;
    }

    if (len == 0) {
        print("usage: fdwrite <text>\n");
        return;
    }

    fd_write(FD_STDOUT, args, len);
    fd_write(FD_STDOUT, "\n", 1);
}

void fd_register_builtin_commands(void) {
    command_register("fdinfo", "show file descriptor table", cmd_fdinfo);
    command_register("fdcat", "cat file through fd_read", cmd_fdcat);
    command_register("fdwrite", "write text through fd_write", cmd_fdwrite);
}
