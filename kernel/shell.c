#include "command.h"
#include "console.h"
#include "fd.h"
#include "print.h"
#include "shell.h"
#include "task.h"
#include "tty.h"
#include "types.h"
#include "vfs.h"

#define SHELL_LINE_MAX 64
#define SHELL_HISTORY_SIZE 16
#define SHELL_HISTORY_LINE_MAX SHELL_LINE_MAX

#define SHELL_PROMPT "my-os> "

static char line_buffer[SHELL_LINE_MAX];
static u32 line_length = 0;
static u32 prompt_row = 0;
static u32 prompt_col = 0;
static u32 last_rendered_length = 0;

static char history[SHELL_HISTORY_SIZE][SHELL_HISTORY_LINE_MAX];
static u32 history_count = 0;
static u32 history_next = 0;
static s32 history_view = -1;
static char history_draft[SHELL_HISTORY_LINE_MAX];
static u32 history_draft_valid = 0;

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

    "history",
    "syscalltest",
    "syscallentrytest",
    "syscallcpuinfo",
    "gdtinfo",
    "ring3test",
    "ring3info",
    "syscallinfo",
    "mouseinfo",
    "initramfsinfo",
    "elflast",
    "elfload",
    "elfinfo",
    "initrun",
    "exec",
    "ps",
    "wait"
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

static void shell_copy_substr(char* dst, const char* src, u32 len, u32 dst_size) {
    if (!dst || dst_size == 0) {
        return;
    }

    u32 i = 0;

    if (src) {
        while (i < len && i + 1 < dst_size) {
            dst[i] = src[i];
            i++;
        }
    }

    dst[i] = '\0';
}

static u32 shell_append_char(char* dst, u32* index, u32 dst_size, char c) {
    if (!dst || !index || *index + 1 >= dst_size) {
        return 0;
    }

    dst[*index] = c;
    *index = *index + 1;
    dst[*index] = '\0';

    return 1;
}

static u32 shell_append_string(char* dst, u32* index, u32 dst_size, const char* src) {
    if (!dst || !index || !src) {
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

static void shell_draw_prompt(void) {
    print_color(SHELL_PROMPT, COLOR_GREEN_ON_BLACK);
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
    console_set_cursor(prompt_row, prompt_col + line_length);
}

static void shell_replace_line(const char* text) {
    shell_copy_line(line_buffer, text, SHELL_LINE_MAX);
    line_length = shell_strlen(line_buffer);
    shell_redraw_line();
}

static void shell_print_prompt(void) {
    shell_draw_prompt();
    console_get_cursor(&prompt_row, &prompt_col);

    line_length = 0;
    last_rendered_length = 0;
    line_buffer[0] = '\0';

    history_view = -1;
    history_draft_valid = 0;
    history_draft[0] = '\0';
}

static u32 history_physical_index(u32 logical_index) {
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
    if (!line || shell_line_is_blank(line)) {
        return;
    }

    const char* latest = history_latest();

    if (latest && shell_streq(latest, line)) {
        return;
    }

    shell_copy_line(history[history_next], line, SHELL_HISTORY_LINE_MAX);
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

static void shell_history_up(void) {
    if (history_count == 0) {
        return;
    }

    if (history_view < 0) {
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

    history_view = -1;
    shell_replace_line(history_draft_valid ? history_draft : "");
    history_draft_valid = 0;
}

static void shell_reduce_to_common_prefix(char* base, const char* other) {
    if (!base || !other) {
        return;
    }

    u32 i = 0;

    while (base[i] && other[i] && base[i] == other[i]) {
        i++;
    }

    base[i] = '\0';
}

static void shell_apply_command_completion(const char* command) {
    char completed[SHELL_LINE_MAX];

    shell_copy_line(completed, command, sizeof(completed));

    u32 len = shell_strlen(completed);

    if (len + 1 < SHELL_LINE_MAX) {
        completed[len] = ' ';
        completed[len + 1] = '\0';
    }

    shell_replace_line(completed);
}

static void shell_print_command_matches(const char* prefix) {
    print("\n");

    for (u32 i = 0; i < SHELL_COMPLETION_COUNT; i++) {
        const char* candidate = shell_completion_commands[i];

        if (shell_starts_with(candidate, prefix)) {
            print("  ");
            print(candidate);
            print("\n");
        }
    }

    shell_draw_prompt();
    console_get_cursor(&prompt_row, &prompt_col);
    print(line_buffer);
    last_rendered_length = line_length;
}

static u32 shell_complete_command(void) {
    for (u32 i = 0; i < line_length; i++) {
        if (shell_is_space(line_buffer[i])) {
            return 0;
        }
    }

    char prefix[SHELL_LINE_MAX];
    shell_copy_line(prefix, line_buffer, sizeof(prefix));

    const char* last_match = 0;
    u32 count = 0;
    char common[SHELL_LINE_MAX];
    u32 common_set = 0;

    for (u32 i = 0; i < SHELL_COMPLETION_COUNT; i++) {
        const char* candidate = shell_completion_commands[i];

        if (!shell_starts_with(candidate, prefix)) {
            continue;
        }

        count++;
        last_match = candidate;

        if (!common_set) {
            shell_copy_line(common, candidate, sizeof(common));
            common_set = 1;
        } else {
            shell_reduce_to_common_prefix(common, candidate);
        }
    }

    if (count == 0) {
        return 0;
    }

    history_reset_view();

    if (count == 1 && last_match) {
        shell_apply_command_completion(last_match);
        return 1;
    }

    if (common_set && shell_strlen(common) > shell_strlen(prefix)) {
        shell_replace_line(common);
        shell_print_command_matches(common);
        return 1;
    }

    shell_print_command_matches(prefix);
    return 1;
}

static u32 shell_is_path_command(const char* command) {
    return shell_streq(command, "ls") ||
           shell_streq(command, "cat") ||
           shell_streq(command, "fdcat") ||
           shell_streq(command, "elfinfo") ||
           shell_streq(command, "elfload") ||
           shell_streq(command, "initrun") ||
           shell_streq(command, "exec");
}

static u32 shell_extract_command(char* out, u32 out_size) {
    if (!out || out_size == 0) {
        return 0;
    }

    out[0] = '\0';

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

    if (token_len == 0) {
        shell_copy_line(parent_path, "/", parent_size);
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
            shell_copy_substr(prefix, token + 1, token_len - 1, prefix_size);
        }

        return 1;
    }

    shell_copy_substr(parent_path, token, last_slash, parent_size);

    if (last_slash + 1 < token_len) {
        shell_copy_substr(prefix, token + last_slash + 1, token_len - last_slash - 1, prefix_size);
    }

    return 1;
}

static u32 shell_build_child_path(const char* parent_path, const char* child_name, char* out, u32 out_size) {
    u32 index = 0;

    if (!parent_path || !child_name || !out || out_size == 0) {
        return 0;
    }

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

    return shell_append_string(out, &index, out_size, child_name);
}

static void shell_replace_path_token(u32 token_start, const char* completed_path, vfs_node_t* node) {
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

    if (node) {
        shell_append_char(new_line, &index, sizeof(new_line), node->type == VFS_NODE_DIR ? '/' : ' ');
    }

    shell_replace_line(new_line);
}

static void shell_print_path_matches(const char* parent_path, const char* prefix) {
    vfs_node_t* parent = vfs_lookup(parent_path);

    if (!parent || parent->type != VFS_NODE_DIR) {
        return;
    }

    print("\n");

    vfs_node_t* child = parent->first_child;

    while (child) {
        if (shell_starts_with(child->name, prefix)) {
            print("  ");

            if (parent_path && parent_path[0] && !shell_streq(parent_path, "/")) {
                print(parent_path);
                print("/");
            } else {
                print("/");
            }

            print(child->name);

            if (child->type == VFS_NODE_DIR) {
                print("/");
            }

            print("\n");
        }

        child = child->next_sibling;
    }

    shell_draw_prompt();
    console_get_cursor(&prompt_row, &prompt_col);
    print(line_buffer);
    last_rendered_length = line_length;
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

    if (!shell_make_path_context(token_start, parent_path, sizeof(parent_path), prefix, sizeof(prefix))) {
        return 0;
    }

    vfs_node_t* parent = vfs_lookup(parent_path);

    if (!parent || parent->type != VFS_NODE_DIR) {
        return 1;
    }

    u32 count = 0;
    vfs_node_t* last_match = 0;
    char common_name[SHELL_LINE_MAX];
    u32 common_set = 0;
    vfs_node_t* child = parent->first_child;

    while (child) {
        if (shell_starts_with(child->name, prefix)) {
            count++;
            last_match = child;

            if (!common_set) {
                shell_copy_line(common_name, child->name, sizeof(common_name));
                common_set = 1;
            } else {
                shell_reduce_to_common_prefix(common_name, child->name);
            }
        }

        child = child->next_sibling;
    }

    if (count == 0) {
        return 1;
    }

    history_reset_view();

    if (count == 1 && last_match) {
        char completed_path[SHELL_LINE_MAX];

        if (shell_build_child_path(parent_path, last_match->name, completed_path, sizeof(completed_path))) {
            shell_replace_path_token(token_start, completed_path, last_match);
        }

        return 1;
    }

    if (common_set && shell_strlen(common_name) > shell_strlen(prefix)) {
        char completed_path[SHELL_LINE_MAX];

        if (shell_build_child_path(parent_path, common_name, completed_path, sizeof(completed_path))) {
            shell_replace_path_token(token_start, completed_path, 0);
            shell_print_path_matches(parent_path, common_name);
        }

        return 1;
    }

    shell_print_path_matches(parent_path, prefix);

    return 1;
}

static void shell_complete_tab(void) {
    if (shell_complete_path()) {
        return;
    }

    shell_complete_command();
}

static void shell_backspace(void) {
    if (line_length == 0) {
        return;
    }

    line_length--;
    line_buffer[line_length] = '\0';
    shell_redraw_line();
}

static void shell_on_char(char c) {
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
        shell_backspace();
        return;
    }

    if (line_length + 1 >= SHELL_LINE_MAX) {
        return;
    }

    line_buffer[line_length] = c;
    line_length++;
    line_buffer[line_length] = '\0';

    shell_redraw_line();
}

static void shell_on_key(tty_key_t key) {
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
        shell_backspace();
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

static void shell_task(void* arg) {
    (void)arg;

    for (;;) {
        tty_key_t key;
        s64 read = fd_read(FD_STDIN, &key, sizeof(key));

        if (read == (s64)sizeof(key)) {
            shell_on_key(key);
        }
    }
}

void shell_start_task(void) {
    u32 id = task_create("shell", shell_task, 0);

    if (id == 0) {
        print_color("failed to create shell task\n", COLOR_RED_ON_BLACK);
        return;
    }
}
