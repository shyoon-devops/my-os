#include "command.h"
#include "console.h"
#include "print.h"
#include "shell.h"
#include "task.h"
#include "tty.h"
#include "types.h"
#include "vfs.h"

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

/*
 * Phase 5-B:
 *   최소 명령어 자동완성 후보 목록.
 *
 * 지금은 shell 내부 static list로 시작한다.
 * 나중에 command registry에서 등록된 명령어를 순회하는 API를 만들면
 * 이 목록은 제거할 수 있다.
 */
static const char* shell_completion_commands[] = {
    "help",
    "clear",
    "ticks",
    "mem",
    "echo",

    "pmmtest",
    "vmmtest",
    "heaptest",
    "pf",

    "cmds",

    "devices",
    "devcount",

    "dmesg",
    "dmesgclear",
    "dmesginfo",

    "panic",
    "asserttest",

    "wqtest",
    "wqstats",
    "wqrun",

    "delaytest",
    "timerstats",

    "date",
    "rtctest",

    "tasks",
    "taskdemo",
    "sleepdemo",
    "yield",
    "taskstats",

    "ls",
    "cat",
    "fsinfo",

    "fdinfo",
    "fdcat",
    "fdwrite",

    "history"
};

#define SHELL_COMPLETION_COUNT \
    (sizeof(shell_completion_commands) / sizeof(shell_completion_commands[0]))

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

static u32 shell_starts_with(const char* s, const char* prefix) {
    if (!s || !prefix) {
        return 0;
    }

    while (*prefix) {
        if (*s != *prefix) {
            return 0;
        }

        s++;
        prefix++;
    }

    return 1;
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

static void shell_copy_substr(
    char* dst,
    const char* src,
    u32 len,
    u32 dst_size
) {
    if (!dst || dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    u32 i = 0;

    while (i < len && i + 1 < dst_size) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static u32 shell_append_char(char* dst, u32* index, u32 dst_size, char c) {
    if (!dst || !index || dst_size == 0) {
        return 0;
    }

    if (*index + 1 >= dst_size) {
        return 0;
    }

    dst[*index] = c;
    *index = *index + 1;
    dst[*index] = '\0';

    return 1;
}

static u32 shell_append_string(
    char* dst,
    u32* index,
    u32 dst_size,
    const char* src
) {
    if (!dst || !index || !src || dst_size == 0) {
        return 0;
    }

    while (*src) {
        if (!shell_append_char(dst, index, dst_size, *src)) {
            return 0;
        }

        src++;
    }

    return 1;
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

static void shell_reprint_prompt_with_line(const char* line) {
    print_color("my-os> ", COLOR_GREEN_ON_BLACK);

    console_get_cursor(&prompt_row, &prompt_col);

    line_length = 0;
    cursor_index = 0;
    last_rendered_length = 0;
    line_buffer[0] = '\0';

    shell_replace_line(line);
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

static u32 shell_get_command_prefix(char* out, u32 out_size) {
    if (!out || out_size == 0) {
        return 0;
    }

    out[0] = '\0';

    /*
     * 이번 최소 구현에서는 cursor가 줄 끝에 있을 때만 자동완성한다.
     * 중간 커서 위치에서 완성하면 line editor 처리가 복잡해지므로
     * 나중 단계로 미룬다.
     */
    if (cursor_index != line_length) {
        return 0;
    }

    /*
     * command 이름만 자동완성한다.
     * 즉 공백이 있으면 command completion 대상이 아니다.
     */
    for (u32 i = 0; i < line_length; i++) {
        if (shell_is_space(line_buffer[i])) {
            return 0;
        }
    }

    u32 i = 0;

    while (i < line_length && i + 1 < out_size) {
        out[i] = line_buffer[i];
        i++;
    }

    out[i] = '\0';

    return 1;
}

static u32 shell_count_completion_matches(
    const char* prefix,
    const char** last_match
) {
    u32 count = 0;

    if (last_match) {
        *last_match = 0;
    }

    for (u32 i = 0; i < SHELL_COMPLETION_COUNT; i++) {
        const char* candidate = shell_completion_commands[i];

        if (shell_starts_with(candidate, prefix)) {
            count++;

            if (last_match) {
                *last_match = candidate;
            }
        }
    }

    return count;
}

static void shell_apply_completion(const char* command) {
    char completed[SHELL_LINE_MAX];

    shell_copy_line(completed, command, sizeof(completed));

    u32 len = shell_strlen(completed);

    /*
     * 명령어가 완성되면 뒤에 공백 하나를 붙여서 바로 인자를 칠 수 있게 한다.
     */
    if (len + 1 < SHELL_LINE_MAX) {
        completed[len] = ' ';
        completed[len + 1] = '\0';
    }

    shell_replace_line(completed);
}

static void shell_print_completion_matches(const char* prefix) {
    char saved[SHELL_LINE_MAX];

    shell_copy_line(saved, line_buffer, sizeof(saved));

    console_put_char('\n');

    for (u32 i = 0; i < SHELL_COMPLETION_COUNT; i++) {
        const char* candidate = shell_completion_commands[i];

        if (shell_starts_with(candidate, prefix)) {
            print("  ");
            print(candidate);
            print("\n");
        }
    }

    shell_reprint_prompt_with_line(saved);
}

static void shell_complete_command(void) {
    char prefix[SHELL_LINE_MAX];

    if (!shell_get_command_prefix(prefix, sizeof(prefix))) {
        return;
    }

    const char* match = 0;
    u32 count = shell_count_completion_matches(prefix, &match);

    if (count == 0) {
        return;
    }

    history_reset_view();

    if (count == 1 && match) {
        shell_apply_completion(match);
        return;
    }

    shell_print_completion_matches(prefix);
}

static u32 shell_is_path_command(const char* command) {
    /*
     * 현재 VFS path를 받는 명령만 path autocomplete 대상으로 둔다.
     *
     * 나중에 open/stat/rm/mkdir 등이 생기면 여기에 추가하거나,
     * command registry에 "path arg 지원" metadata를 넣는 방식으로 바꿀 수 있다.
     */
    return shell_streq(command, "ls") ||
           shell_streq(command, "cat") ||
           shell_streq(command, "fdcat");
}

static u32 shell_extract_command(char* out, u32 out_size) {
    if (!out || out_size == 0) {
        return 0;
    }

    out[0] = '\0';

    if (line_length == 0) {
        return 0;
    }

    u32 i = 0;

    while (i < line_length && shell_is_space(line_buffer[i])) {
        i++;
    }

    if (i >= line_length) {
        return 0;
    }

    u32 out_index = 0;

    while (i < line_length && !shell_is_space(line_buffer[i])) {
        if (out_index + 1 >= out_size) {
            break;
        }

        out[out_index] = line_buffer[i];
        out_index++;
        i++;
    }

    out[out_index] = '\0';

    return out_index > 0;
}

static u32 shell_find_path_token_start(u32* out_start) {
    if (!out_start) {
        return 0;
    }

    if (cursor_index != line_length) {
        return 0;
    }

    u32 i = 0;

    while (i < line_length && shell_is_space(line_buffer[i])) {
        i++;
    }

    while (i < line_length && !shell_is_space(line_buffer[i])) {
        i++;
    }

    if (i >= line_length) {
        return 0;
    }

    while (i < line_length && shell_is_space(line_buffer[i])) {
        i++;
    }

    /*
     * 이번 최소 구현은 첫 번째 인자 하나만 autocomplete한다.
     * 즉 "cmd arg1 arg2"에서 arg2 자동완성은 아직 하지 않는다.
     */
    *out_start = i;

    return 1;
}

static u32 shell_make_path_context(
    u32 token_start,
    char* parent_path,
    u32 parent_size,
    char* prefix,
    u32 prefix_size
) {
    if (!parent_path || !prefix || parent_size == 0 || prefix_size == 0) {
        return 0;
    }

    parent_path[0] = '\0';
    prefix[0] = '\0';

    if (token_start > line_length) {
        return 0;
    }

    const char* token = &line_buffer[token_start];
    u32 token_len = line_length - token_start;

    /*
     * VFS는 현재 absolute path만 지원한다.
     *
     * "cat <Tab>"처럼 token이 비어 있으면 root("/") 기준으로 본다.
     * "cat /h<Tab>"처럼 token이 '/'로 시작해야 경로 자동완성을 한다.
     */
    if (token_len == 0) {
        shell_copy_line(parent_path, "/", parent_size);
        prefix[0] = '\0';
        return 1;
    }

    if (token[0] != '/') {
        return 0;
    }

    u32 last_slash = 0;

    for (u32 i = 0; i < token_len; i++) {
        if (token[i] == '/') {
            last_slash = i;
        }
    }

    if (last_slash == 0) {
        shell_copy_line(parent_path, "/", parent_size);

        if (token_len > 1) {
            shell_copy_substr(
                prefix,
                token + 1,
                token_len - 1,
                prefix_size
            );
        } else {
            prefix[0] = '\0';
        }

        return 1;
    }

    shell_copy_substr(parent_path, token, last_slash, parent_size);

    if (last_slash + 1 < token_len) {
        shell_copy_substr(
            prefix,
            token + last_slash + 1,
            token_len - last_slash - 1,
            prefix_size
        );
    } else {
        prefix[0] = '\0';
    }

    return 1;
}

static u32 shell_build_child_path(
    const char* parent_path,
    const char* child_name,
    char* out,
    u32 out_size
) {
    if (!parent_path || !child_name || !out || out_size == 0) {
        return 0;
    }

    u32 index = 0;

    out[0] = '\0';

    if (shell_streq(parent_path, "/")) {
        if (!shell_append_char(out, &index, out_size, '/')) {
            return 0;
        }
    } else {
        if (!shell_append_string(out, &index, out_size, parent_path)) {
            return 0;
        }

        if (!shell_append_char(out, &index, out_size, '/')) {
            return 0;
        }
    }

    if (!shell_append_string(out, &index, out_size, child_name)) {
        return 0;
    }

    return 1;
}

static u32 shell_count_path_matches(
    vfs_node_t* parent,
    const char* prefix,
    vfs_node_t** last_match
) {
    if (last_match) {
        *last_match = 0;
    }

    if (!parent || parent->type != VFS_NODE_DIR || !prefix) {
        return 0;
    }

    u32 count = 0;
    vfs_node_t* child = parent->first_child;

    while (child) {
        if (shell_starts_with(child->name, prefix)) {
            count++;

            if (last_match) {
                *last_match = child;
            }
        }

        child = child->next_sibling;
    }

    return count;
}

static void shell_apply_path_completion(
    u32 token_start,
    const char* completed_path,
    vfs_node_t* node
) {
    if (!completed_path || !node) {
        return;
    }

    char new_line[SHELL_LINE_MAX];
    u32 index = 0;

    new_line[0] = '\0';

    for (u32 i = 0; i < token_start && i < line_length; i++) {
        if (!shell_append_char(new_line, &index, sizeof(new_line), line_buffer[i])) {
            return;
        }
    }

    if (!shell_append_string(new_line, &index, sizeof(new_line), completed_path)) {
        return;
    }

    if (node->type == VFS_NODE_DIR) {
        shell_append_char(new_line, &index, sizeof(new_line), '/');
    } else {
        shell_append_char(new_line, &index, sizeof(new_line), ' ');
    }

    shell_replace_line(new_line);
}

static void shell_print_path_matches(
    const char* parent_path,
    const char* prefix
) {
    if (!parent_path || !prefix) {
        return;
    }

    vfs_node_t* parent = vfs_lookup(parent_path);

    if (!parent || parent->type != VFS_NODE_DIR) {
        return;
    }

    char saved[SHELL_LINE_MAX];

    shell_copy_line(saved, line_buffer, sizeof(saved));

    console_put_char('\n');

    vfs_node_t* child = parent->first_child;

    while (child) {
        if (shell_starts_with(child->name, prefix)) {
            char path[SHELL_LINE_MAX];

            if (shell_build_child_path(parent_path, child->name, path, sizeof(path))) {
                print("  ");
                print(path);

                if (child->type == VFS_NODE_DIR) {
                    print("/");
                }

                print("\n");
            }
        }

        child = child->next_sibling;
    }

    shell_reprint_prompt_with_line(saved);
}

static u32 shell_complete_path(void) {
    char command[SHELL_LINE_MAX];

    if (!shell_extract_command(command, sizeof(command))) {
        return 0;
    }

    if (!shell_is_path_command(command)) {
        return 0;
    }

    u32 token_start = 0;

    if (!shell_find_path_token_start(&token_start)) {
        return 0;
    }

    char parent_path[SHELL_LINE_MAX];
    char prefix[SHELL_LINE_MAX];

    if (!shell_make_path_context(
            token_start,
            parent_path,
            sizeof(parent_path),
            prefix,
            sizeof(prefix)
        )) {
        return 0;
    }

    vfs_node_t* parent = vfs_lookup(parent_path);

    if (!parent || parent->type != VFS_NODE_DIR) {
        return 1;
    }

    vfs_node_t* match = 0;
    u32 count = shell_count_path_matches(parent, prefix, &match);

    if (count == 0) {
        return 1;
    }

    history_reset_view();

    if (count == 1 && match) {
        char completed_path[SHELL_LINE_MAX];

        if (shell_build_child_path(
                parent_path,
                match->name,
                completed_path,
                sizeof(completed_path)
            )) {
            shell_apply_path_completion(token_start, completed_path, match);
        }

        return 1;
    }

    shell_print_path_matches(parent_path, prefix);

    return 1;
}

static void shell_complete_tab(void) {
    /*
     * 공백 뒤에서 path command라면 경로 자동완성을 먼저 시도한다.
     * 그 외에는 명령어 자동완성으로 처리한다.
     */
    if (shell_complete_path()) {
        return;
    }

    shell_complete_command();
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
        console_scroll_page_up();
        return;
    }

    if (key == TTY_KEY_PAGE_DOWN) {
        console_scroll_page_down();
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
        shell_complete_tab();
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
