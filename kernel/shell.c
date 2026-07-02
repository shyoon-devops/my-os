#include "command.h"
#include "console.h"
#include "print.h"
#include "shell.h"
#include "task.h"
#include "tty.h"
#include "types.h"

#define SHELL_LINE_MAX 64

static char line_buffer[SHELL_LINE_MAX];
static u32 line_length = 0;
static u32 cursor_index = 0;

static u32 prompt_row = 0;
static u32 prompt_col = 0;
static u32 last_rendered_length = 0;

static void shell_set_visual_cursor_to_index(u32 index) {
    console_set_cursor(prompt_row, prompt_col + index);
}

static void shell_redraw_line(void) {
    u32 old_len = last_rendered_length;

    console_set_cursor(prompt_row, prompt_col);

    for (u32 i = 0; i < line_length; i++) {
        console_put_char(line_buffer[i]);
    }

    if (old_len > line_length) {
        for (u32 i = line_length; i < old_len; i++) {
            console_put_char(' ');
        }
    }

    last_rendered_length = line_length;

    shell_set_visual_cursor_to_index(cursor_index);
}

static void shell_print_prompt(void) {
    print_color("my-os> ", COLOR_GREEN_ON_BLACK);

    console_get_cursor(&prompt_row, &prompt_col);

    line_length = 0;
    cursor_index = 0;
    last_rendered_length = 0;
    line_buffer[0] = '\0';
}

static void shell_cursor_left(void) {
    if (cursor_index == 0) {
        return;
    }

    cursor_index--;
    shell_set_visual_cursor_to_index(cursor_index);
}

static void shell_cursor_right(void) {
    if (cursor_index >= line_length) {
        return;
    }

    cursor_index++;
    shell_set_visual_cursor_to_index(cursor_index);
}

static void shell_cursor_home(void) {
    cursor_index = 0;
    shell_set_visual_cursor_to_index(cursor_index);
}

static void shell_cursor_end(void) {
    cursor_index = line_length;
    shell_set_visual_cursor_to_index(cursor_index);
}

static void shell_on_char(char c) {
    if (c == '\n') {
        console_put_char('\n');

        line_buffer[line_length] = '\0';
        command_execute(line_buffer);

        shell_print_prompt();
        return;
    }

    if (c == '\b') {
        if (cursor_index == 0) {
            return;
        }

        for (u32 i = cursor_index - 1; i + 1 < line_length; i++) {
            line_buffer[i] = line_buffer[i + 1];
        }

        line_length--;
        cursor_index--;
        line_buffer[line_length] = '\0';

        shell_redraw_line();
        return;
    }

    if (line_length + 1 >= SHELL_LINE_MAX) {
        return;
    }

    for (u32 i = line_length; i > cursor_index; i--) {
        line_buffer[i] = line_buffer[i - 1];
    }

    line_buffer[cursor_index] = c;
    line_length++;
    cursor_index++;
    line_buffer[line_length] = '\0';

    shell_redraw_line();
}

static void shell_on_key(tty_key_t key) {
    if (key == TTY_KEY_LEFT) {
        shell_cursor_left();
        return;
    }

    if (key == TTY_KEY_RIGHT) {
        shell_cursor_right();
        return;
    }

    if (key == TTY_KEY_HOME) {
        shell_cursor_home();
        return;
    }

    if (key == TTY_KEY_END) {
        shell_cursor_end();
        return;
    }

    if (key == TTY_KEY_BACKSPACE) {
        shell_on_char('\b');
        return;
    }

    if (key == TTY_KEY_ENTER) {
        shell_on_char('\n');
        return;
    }

    if (key == TTY_KEY_TAB) {
        shell_on_char(' ');
        shell_on_char(' ');
        shell_on_char(' ');
        shell_on_char(' ');
        return;
    }

    if (key >= 0x20 && key <= 0x7E) {
        shell_on_char((char)key);
        return;
    }
}

void shell_init(void) {
    line_length = 0;
    cursor_index = 0;
    last_rendered_length = 0;
    line_buffer[0] = '\0';

    print_color("[Shell]\n", COLOR_YELLOW_ON_BLACK);
    print("Type 'help' to list commands.\n");

    shell_print_prompt();
}

void shell_poll(void) {
    tty_key_t key;

    while (tty_read_key(&key)) {
        shell_on_key(key);
    }
}

static void shell_task(void* arg) {
    (void)arg;

    for (;;) {
        tty_key_t key;

        /*
         * polling 제거.
         *
         * 입력이 있으면 즉시 하나 읽고,
         * 입력이 없으면 tty wait queue에서 WAITING 상태로 잠든다.
         */
        tty_read_key_blocking(&key);
        shell_on_key(key);
    }
}

void shell_start_task(void) {
    u32 id = task_create("shell", shell_task, 0);

    if (id == 0) {
        print_color("failed to create shell task\n", COLOR_RED_ON_BLACK);
        return;
    }

    print("shell task created: ");
    print_dec64(id);
    print("\n");
}
