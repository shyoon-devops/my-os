#ifndef GO_OS_CONSOLE_H
#define GO_OS_CONSOLE_H

#include "types.h"

#define COLOR_WHITE_ON_BLACK   0x0F
#define COLOR_GREEN_ON_BLACK   0x0A
#define COLOR_RED_ON_BLACK     0x0C
#define COLOR_CYAN_ON_BLACK    0x0B
#define COLOR_YELLOW_ON_BLACK  0x0E

void console_clear(void);

void console_put_char(char c);
void console_backspace(void);

void console_set_color(u8 color);
u8 console_get_color(void);

u32 console_get_row(void);
u32 console_get_col(void);

void console_set_cursor(u32 row, u32 col);
void console_get_cursor(u32* row, u32* col);

#endif