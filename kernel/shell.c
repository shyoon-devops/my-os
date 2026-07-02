#include "command.h"
#include "console.h"
#include "print.h"
#include "shell.h"
#include "task.h"
#include "tty.h"
#include "types.h"

#define SHELL_LINE_MAX 64

#define SHELL_HISTORY_SIZE 16
#define SHELL_HISTORY_LINE_MAX SHELL_LINE_MAX

static char line_buffer[SHELL_LINE_MAX];
static u32 line_length = 0;
static u32 cursor_index = 0;

static u32 prompt_row = 0;
static u32 prompt_col = 0;
static u32 last_rendered_length = 0;

/*
 * history는 ring buffer다.
 *
 * history_next:
 *   다음 명령을 저장할 물리 index.
 *
 * history_count:
 *   현재 저장된 명령 개수. 최대 SHELL_HISTORY_SIZE.
 *
 * history_view:
 *   현재 ↑/↓로 보고 있는 logical index.
 *   -1이면 history 탐색 중이 아님.
 */
static char history[SHELL_HISTORY_SIZE][SHELL_HISTORY_LINE_MAX];
static u32 history_count = 0;
static u32 history_next = 0;
static s32 history_view = -1;

static char history_draft[SHELL_HISTORY_LINE_MAX];
static u32 history_draft_valid = 0;

static u32 shell_strlen(const char* s) {
    u32 len = 0;

    if (!s) {
        return 0;
    }

    while (s[len]) {
        len++;
    }

    return len;
}

static u32 shell_is_space(char c) {
    return c == ' ' || c == '\t';
}

static u32 shell_line_is_blank(const char* s) {
    if (!s) {
        return 1;
    }

    while (*s) {
        if (!shell_is_space(*s)) {
            return 0;
        }

        s++;
    }

    return 1;
}

static u32 shell_streq(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }

    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }

        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static void shell_copy_line(char* dst, const char* src, u32 dst_size) {
    if (!dst || dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    u32 i = 0;

    while (src[i] && i + 1 < dst_size) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static u32 history_physical_index(u32 logical_index) {
    /*
     * logical index:
     *   0 = 가장 오래된 명령
     *   history_count - 1 = 가장 최근 명령
     *
     * buffer가 아직 가득 차지 않았으면 logical == physical.
     * 가득 찼으면 history_next가 가장 오래된 항목이다.
     */
    if (history_count < SHELL_HISTORY_SIZE) {
        return logical_index;
    }

    return (history_next + logical_index) % SHELL_HISTORY_SIZE;
}

static const char* history_get(u32 logical_index) {
    if (logical_index >= history_count) {
        return 0;
    }

    return history[history_physical_index(logical_index)];
}

static const char* history_latest(void) {
    if (history_count == 0) {
        return 0;
    }

    return history_get(history_count - 1);
}

static void history_add(const char* line) {
    if (!line) {
        return;
    }

    if (shell_line_is_blank(line)) {
        return;
    }

    /*
     * 같은 명령을 연속으로 입력하면 중복 저장하지 않는다.
     */
    const char* latest = history_latest();

    if (latest && shell_streq(latest, line)) {
        return;
    }

    shell_copy_line(
        history[history_next],
        line,
        SHELL_HISTORY_LINE_MAX
    );

    history_next = (history_next + 1) % SHELL_HISTORY_SIZE;

    if (history_count < SHELL_HISTORY_SIZE) {
        history_count++;
    }
}

static void history_reset_view(void) {
    history_view = -1;
    history_draft_valid = 0;
    history_draft[0] = '\0';
}

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

static void shell_replace_line(const char* text) {
    shell_copy_line(line_buffer, text, SHELL_LINE_MAX);

    line_length = shell_strlen(line_buffer);
    cursor_index = line_length;

    shell_redraw_line();
}

static void shell_print_prompt(void) {
    print_color("my-os> ", COLOR_GREEN_ON_BLACK);

    console_get_cursor(&prompt_row, &prompt_col);

    line_length = 0;
    cursor_index = 0;
    last_rendered_length = 0;
    line_buffer[0] = '\0';

    history_reset_view();
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

static void shell_history_up(void) {
    if (history_count == 0) {
        return;
    }

    if (history_view < 0) {
        /*
         * history 탐색을 처음 시작할 때 현재 입력 중이던 내용을 저장한다.
         * ↓로 끝까지 내려오면 이 draft를 복원한다.
         */
        shell_copy_line(history_draft, line_buffer, SHELL_HISTORY_LINE_MAX);
        history_draft_valid = 1;

        history_view = (s32)(history_count - 1);
    } else if (history_view > 0) {
        history_view--;
    }

    const char* line = history_get((u32)history_view);

    if (line) {
        shell_replace_line(line);
    }
}

static void shell_history_down(void) {
    if (history_count == 0) {
        return;
    }

    if (history_view < 0) {
        return;
    }

    if ((u32)(history_view + 1) < history_count) {
        history_view++;

        const char* line = history_get((u32)history_view);

        if (line) {
            shell_replace_line(line);
        }

        return;
    }

    /*
     * 가장 최신 history에서 한 번 더 ↓를 누르면,
     * history 탐색 전에 입력 중이던 draft로 돌아간다.
     */
    history_view = -1;

    if (history_draft_valid) {
        shell_replace_line(history_draft);
    } else {
        shell_replace_line("");
    }

    history_draft_valid = 0;
}

static void shell_on_char(char c) {
    /*
     * 사용자가 새 문자를 입력하거나 편집하면
     * history 탐색 상태는 끝난 것으로 본다.
     */
    if (c != '\n') {
        history_reset_view();
    }

    if (c == '\n') {
        console_put_char('\n');

        line_buffer[line_length] = '\0';

        history_add(line_buffer);
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

    if (key == TTY_KEY_UP) {
        shell_history_up();
        return;
    }

    if (key == TTY_KEY_DOWN) {
        shell_history_down();
        return;
    }

    if (key == TTY_KEY_PAGE_UP) {
        /*
         * 다음 단계 Phase 5-B/5-C에서 console scrollback과 연결한다.
         */
        return;
    }

    if (key == TTY_KEY_PAGE_DOWN) {
        /*
         * 다음 단계 Phase 5-B/5-C에서 console scrollback과 연결한다.
         */
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

static void cmd_history(const char* args) {
    (void)args;

    if (history_count == 0) {
        print("history is empty\n");
        return;
    }

    for (u32 i = 0; i < history_count; i++) {
        const char* line = history_get(i);

        print_dec64(i + 1);
        print("  ");

        if (line) {
            print(line);
        }

        print("\n");
    }
}

void shell_init(void) {
    line_length = 0;
    cursor_index = 0;
    last_rendered_length = 0;
    line_buffer[0] = '\0';

    history_count = 0;
    history_next = 0;
    history_reset_view();

    for (u32 i = 0; i < SHELL_HISTORY_SIZE; i++) {
        history[i][0] = '\0';
    }

    command_register("history", "show shell command history", cmd_history);

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
