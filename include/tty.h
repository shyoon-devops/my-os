#ifndef MY_OS_TTY_H
#define MY_OS_TTY_H

#include "types.h"
#include "vfs.h"

/*
 * tty_key_t:
 *   ASCII 문자는 그대로 0x00~0x7F 범위 사용.
 *   특수키는 0x0100 이상 값 사용.
 */
typedef u16 tty_key_t;

#define TTY_KEY_BACKSPACE  0x0008
#define TTY_KEY_TAB        0x0009
#define TTY_KEY_ENTER      0x000A
#define TTY_KEY_ESC        0x001B

#define TTY_KEY_LEFT       0x0101
#define TTY_KEY_RIGHT      0x0102
#define TTY_KEY_HOME       0x0103
#define TTY_KEY_END        0x0104
#define TTY_KEY_UP         0x0105
#define TTY_KEY_DOWN       0x0106
#define TTY_KEY_PAGE_UP    0x0107
#define TTY_KEY_PAGE_DOWN  0x0108

void tty_init(void);

u32 tty_push_key(tty_key_t key);
u32 tty_push_char(char c);

u32 tty_read_key(tty_key_t* out);

/*
 * 키가 들어올 때까지 현재 task를 wait queue에서 재운다.
 */
void tty_read_key_blocking(tty_key_t* out);

u32 tty_available(void);

/*
 * /dev/tty0 VFS device callbacks.
 *
 * read:
 *   tty_key_t 하나를 blocking read로 읽는다.
 *
 * write:
 *   byte stream을 console에 출력한다.
 */
u64 tty_vfs_read(vfs_node_t* node, u64 offset, void* buffer, u64 size);
u64 tty_vfs_write(vfs_node_t* node, u64 offset, const void* buffer, u64 size);

#endif
