#ifndef MY_OS_CONSOLE_H
#define MY_OS_CONSOLE_H

#include "types.h"

#define CONSOLE_WIDTH  80
#define CONSOLE_HEIGHT 25

#define CONSOLE_SCROLLBACK_LINES 512

#define VGA_COLOR(fg, bg) ((u8)((bg << 4) | (fg)))

#define VGA_COLOR_BLACK         0
#define VGA_COLOR_BLUE          1
#define VGA_COLOR_GREEN         2
#define VGA_COLOR_CYAN          3
#define VGA_COLOR_RED           4
#define VGA_COLOR_MAGENTA       5
#define VGA_COLOR_BROWN         6
#define VGA_COLOR_LIGHT_GREY    7
#define VGA_COLOR_DARK_GREY     8
#define VGA_COLOR_LIGHT_BLUE    9
#define VGA_COLOR_LIGHT_GREEN   10
#define VGA_COLOR_LIGHT_CYAN    11
#define VGA_COLOR_LIGHT_RED     12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_LIGHT_BROWN   14
#define VGA_COLOR_WHITE         15

#define COLOR_WHITE_ON_BLACK  VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK)
#define COLOR_GREEN_ON_BLACK  VGA_COLOR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK)
#define COLOR_RED_ON_BLACK    VGA_COLOR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK)
#define COLOR_YELLOW_ON_BLACK VGA_COLOR(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK)
#define COLOR_CYAN_ON_BLACK   VGA_COLOR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK)

void console_clear(void);

void console_put_char(char c);
void console_write(const char* s);

void console_set_color(u8 color);
u8 console_get_color(void);

void console_set_cursor(u32 row, u32 col);
void console_get_cursor(u32* row, u32* col);

void console_scroll_page_up(void);
void console_scroll_page_down(void);
void console_scroll_line_up(void);
void console_scroll_line_down(void);
void console_scroll_to_bottom(void);

u32 console_is_scrolled(void);
u32 console_scrollback_count(void);
u32 console_viewport_offset(void);

u32 console_width(void);
u32 console_height(void);


u32 console_get_cursor_row(void);
u32 console_get_cursor_col(void);
void console_set_cursor(u32 row, u32 col);
void console_clear_row(u32 row);
u32 console_row_count(void);
u32 console_col_count(void);

#endif
