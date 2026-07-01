#include "console.h"
#include "io.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile u16*)0xB8000)

#define VGA_ENTRY(ch, color) (((u16)(color) << 8) | (u8)(ch))

#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA  0x3D5

static u32 cursor_row = 0;
static u32 cursor_col = 0;
static u8 current_color = COLOR_WHITE_ON_BLACK;

static void console_update_hardware_cursor(void) {
    u16 pos = (u16)(cursor_row * VGA_WIDTH + cursor_col);

    /*
     * VGA text mode cursor position register.
     *
     * index 0x0F = cursor location low byte
     * index 0x0E = cursor location high byte
     */
    outb(VGA_CRTC_INDEX, 0x0F);
    outb(VGA_CRTC_DATA, (u8)(pos & 0xFF));

    outb(VGA_CRTC_INDEX, 0x0E);
    outb(VGA_CRTC_DATA, (u8)((pos >> 8) & 0xFF));
}

static void clear_row(u32 row) {
    for (u32 x = 0; x < VGA_WIDTH; x++) {
        VGA_MEMORY[row * VGA_WIDTH + x] =
            VGA_ENTRY(' ', COLOR_WHITE_ON_BLACK);
    }
}

static void scroll_one_line(void) {
    for (u32 y = 1; y < VGA_HEIGHT; y++) {
        for (u32 x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] =
                VGA_MEMORY[y * VGA_WIDTH + x];
        }
    }

    clear_row(VGA_HEIGHT - 1);
}

static void scroll_if_needed(void) {
    while (cursor_row >= VGA_HEIGHT) {
        scroll_one_line();
        cursor_row = VGA_HEIGHT - 1;
        cursor_col = 0;
    }

    console_update_hardware_cursor();
}

void console_clear(void) {
    for (u32 y = 0; y < VGA_HEIGHT; y++) {
        clear_row(y);
    }

    cursor_row = 0;
    cursor_col = 0;
    current_color = COLOR_WHITE_ON_BLACK;

    console_update_hardware_cursor();
}

void console_put_char(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
        scroll_if_needed();
        return;
    }

    VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] =
        VGA_ENTRY(c, current_color);

    cursor_col++;

    if (cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        cursor_row++;
    }

    scroll_if_needed();
}

void console_backspace(void) {
    if (cursor_col > 0) {
        cursor_col--;
    } else {
        if (cursor_row == 0) {
            console_update_hardware_cursor();
            return;
        }

        cursor_row--;
        cursor_col = VGA_WIDTH - 1;
    }

    VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] =
        VGA_ENTRY(' ', current_color);

    console_update_hardware_cursor();
}

void console_set_color(u8 color) {
    current_color = color;
}

u8 console_get_color(void) {
    return current_color;
}

u32 console_get_row(void) {
    return cursor_row;
}

u32 console_get_col(void) {
    return cursor_col;
}

void console_set_cursor(u32 row, u32 col) {
    if (row >= VGA_HEIGHT) {
        row = VGA_HEIGHT - 1;
    }

    if (col >= VGA_WIDTH) {
        col = VGA_WIDTH - 1;
    }

    cursor_row = row;
    cursor_col = col;

    console_update_hardware_cursor();
}

void console_get_cursor(u32* row, u32* col) {
    if (row) {
        *row = cursor_row;
    }

    if (col) {
        *col = cursor_col;
    }
}