#include "console.h"
#include "io.h"
#include "types.h"

#define VGA_MEMORY ((volatile u16*)0xB8000)

#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA  0x3D5

typedef struct console_cell {
    char ch;
    u8 color;
} console_cell_t;

static console_cell_t scrollback[CONSOLE_SCROLLBACK_LINES][CONSOLE_WIDTH];

static u64 current_line = 0;
static u32 current_col = 0;

static u64 viewport_top = 0;

static u8 current_color = COLOR_WHITE_ON_BLACK;

static u64 min_u64(u64 a, u64 b) {
    return a < b ? a : b;
}

static u64 scrollback_oldest_line(void) {
    if (current_line + 1 <= CONSOLE_SCROLLBACK_LINES) {
        return 0;
    }

    return current_line + 1 - CONSOLE_SCROLLBACK_LINES;
}

static u64 scrollback_bottom_top(void) {
    if (current_line + 1 <= CONSOLE_HEIGHT) {
        return 0;
    }

    return current_line + 1 - CONSOLE_HEIGHT;
}

static u32 physical_line(u64 logical_line) {
    return (u32)(logical_line % CONSOLE_SCROLLBACK_LINES);
}

static void clear_scrollback_line(u64 logical_line) {
    u32 physical = physical_line(logical_line);

    for (u32 col = 0; col < CONSOLE_WIDTH; col++) {
        scrollback[physical][col].ch = ' ';
        scrollback[physical][col].color = current_color;
    }
}

static u16 vga_entry(char ch, u8 color) {
    return (u16)((u16)color << 8) | (u8)ch;
}

static void vga_crtc_write(u8 index, u8 value) {
    outb(VGA_CRTC_INDEX, index);
    outb(VGA_CRTC_DATA, value);
}

static void hardware_cursor_set(u32 row, u32 col) {
    if (row >= CONSOLE_HEIGHT) {
        row = CONSOLE_HEIGHT - 1;
    }

    if (col >= CONSOLE_WIDTH) {
        col = CONSOLE_WIDTH - 1;
    }

    u16 pos = (u16)(row * CONSOLE_WIDTH + col);

    vga_crtc_write(0x0F, (u8)(pos & 0xFF));
    vga_crtc_write(0x0E, (u8)((pos >> 8) & 0xFF));
}

static void render_viewport(void) {
    u64 oldest = scrollback_oldest_line();
    u64 bottom = scrollback_bottom_top();

    if (viewport_top < oldest) {
        viewport_top = oldest;
    }

    if (viewport_top > bottom) {
        viewport_top = bottom;
    }

    for (u32 row = 0; row < CONSOLE_HEIGHT; row++) {
        u64 logical = viewport_top + row;

        for (u32 col = 0; col < CONSOLE_WIDTH; col++) {
            char ch = ' ';
            u8 color = current_color;

            if (logical >= oldest && logical <= current_line) {
                console_cell_t cell =
                    scrollback[physical_line(logical)][col];

                ch = cell.ch;
                color = cell.color;
            }

            VGA_MEMORY[row * CONSOLE_WIDTH + col] = vga_entry(ch, color);
        }
    }

    if (current_line >= viewport_top &&
        current_line < viewport_top + CONSOLE_HEIGHT) {
        hardware_cursor_set((u32)(current_line - viewport_top), current_col);
    } else {
        hardware_cursor_set(CONSOLE_HEIGHT - 1, CONSOLE_WIDTH - 1);
    }
}

static u32 viewport_is_bottom(void) {
    return viewport_top == scrollback_bottom_top();
}

static void move_to_next_line(void) {
    current_line++;
    current_col = 0;

    clear_scrollback_line(current_line);

    if (viewport_is_bottom()) {
        viewport_top = scrollback_bottom_top();
    }
}

void console_clear(void) {
    current_line = 0;
    current_col = 0;
    viewport_top = 0;
    current_color = COLOR_WHITE_ON_BLACK;

    for (u64 line = 0; line < CONSOLE_SCROLLBACK_LINES; line++) {
        clear_scrollback_line(line);
    }

    render_viewport();
}

void console_set_color(u8 color) {
    current_color = color;
}

u8 console_get_color(void) {
    return current_color;
}

void console_put_char(char c) {
    /*
     * 지금 정책:
     *   새 출력이 발생하면 항상 현재 화면(bottom)으로 복귀한다.
     *
     * 그래서 과거 로그를 보고 있다가 명령어를 치면,
     * 자연스럽게 최신 프롬프트 위치로 돌아온다.
     */
    viewport_top = scrollback_bottom_top();

    if (c == '\n') {
        move_to_next_line();
        render_viewport();
        return;
    }

    if (c == '\r') {
        current_col = 0;
        render_viewport();
        return;
    }

    if (c == '\t') {
        for (u32 i = 0; i < 4; i++) {
            console_put_char(' ');
        }

        return;
    }

    if (c == '\b') {
        if (current_col > 0) {
            current_col--;

            console_cell_t* cell =
                &scrollback[physical_line(current_line)][current_col];

            cell->ch = ' ';
            cell->color = current_color;
        }

        render_viewport();
        return;
    }

    if (current_col >= CONSOLE_WIDTH) {
        move_to_next_line();
    }

    console_cell_t* cell =
        &scrollback[physical_line(current_line)][current_col];

    cell->ch = c;
    cell->color = current_color;

    current_col++;

    if (current_col >= CONSOLE_WIDTH) {
        move_to_next_line();
    }

    render_viewport();
}

void console_write(const char* s) {
    if (!s) {
        return;
    }

    while (*s) {
        console_put_char(*s);
        s++;
    }
}

void console_set_cursor(u32 row, u32 col) {
    if (row >= CONSOLE_HEIGHT) {
        row = CONSOLE_HEIGHT - 1;
    }

    if (col >= CONSOLE_WIDTH) {
        col = CONSOLE_WIDTH - 1;
    }

    viewport_top = scrollback_bottom_top();

    current_line = viewport_top + row;
    current_col = col;

    render_viewport();
}

void console_get_cursor(u32* row, u32* col) {
    u32 visible_row = 0;

    if (current_line >= viewport_top) {
        visible_row = (u32)(current_line - viewport_top);
    }

    if (visible_row >= CONSOLE_HEIGHT) {
        visible_row = CONSOLE_HEIGHT - 1;
    }

    if (row) {
        *row = visible_row;
    }

    if (col) {
        *col = current_col;
    }
}

void console_scroll_page_up(void) {
    u64 oldest = scrollback_oldest_line();

    if (viewport_top <= oldest) {
        viewport_top = oldest;
        render_viewport();
        return;
    }

    u64 amount = CONSOLE_HEIGHT;

    if (viewport_top < oldest + amount) {
        viewport_top = oldest;
    } else {
        viewport_top -= amount;
    }

    render_viewport();
}

void console_scroll_page_down(void) {
    u64 bottom = scrollback_bottom_top();

    if (viewport_top >= bottom) {
        viewport_top = bottom;
        render_viewport();
        return;
    }

    viewport_top = min_u64(viewport_top + CONSOLE_HEIGHT, bottom);

    render_viewport();
}

void console_scroll_line_up(void) {
    u64 oldest = scrollback_oldest_line();

    if (viewport_top <= oldest) {
        viewport_top = oldest;
        render_viewport();
        return;
    }

    viewport_top--;
    render_viewport();
}

void console_scroll_line_down(void) {
    u64 bottom = scrollback_bottom_top();

    if (viewport_top >= bottom) {
        viewport_top = bottom;
        render_viewport();
        return;
    }

    viewport_top++;
    render_viewport();
}

void console_scroll_to_bottom(void) {
    viewport_top = scrollback_bottom_top();
    render_viewport();
}

u32 console_is_scrolled(void) {
    return viewport_top != scrollback_bottom_top();
}

u32 console_scrollback_count(void) {
    u64 count = current_line + 1;

    return (u32)min_u64(count, CONSOLE_SCROLLBACK_LINES);
}

u32 console_viewport_offset(void) {
    u64 bottom = scrollback_bottom_top();

    if (viewport_top >= bottom) {
        return 0;
    }

    return (u32)(bottom - viewport_top);
}

u32 console_width(void) {
    return CONSOLE_WIDTH;
}

u32 console_height(void) {
    return CONSOLE_HEIGHT;
}
