#include "command.h"
#include "console.h"
#include "heap.h"
#include "idt.h"
#include "pit.h"
#include "pmm.h"
#include "print.h"
#include "types.h"
#include "vmm.h"

typedef struct command {
    char* name;
    char* description;
    command_handler_t handler;

    struct command* next;
} command_t;

static command_t* command_head = 0;
static command_t* command_tail = 0;
static u32 command_count = 0;

static u64 command_strlen(const char* s) {
    u64 len = 0;

    if (!s) {
        return 0;
    }

    while (s[len]) {
        len++;
    }

    return len;
}

static u32 command_streq(const char* a, const char* b) {
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

static char* command_strdup(const char* s) {
    if (!s) {
        return 0;
    }

    u64 len = command_strlen(s);
    char* copy = (char*)kmalloc(len + 1);

    if (!copy) {
        return 0;
    }

    for (u64 i = 0; i < len; i++) {
        copy[i] = s[i];
    }

    copy[len] = '\0';

    return copy;
}

static const char* command_skip_spaces(const char* s) {
    while (*s == ' ') {
        s++;
    }

    return s;
}

static u32 command_token_matches(
    const char* line,
    const char* command_name,
    const char** args_out
) {
    const char* p = line;
    const char* n = command_name;

    while (*n) {
        if (*p != *n) {
            return 0;
        }

        p++;
        n++;
    }

    /*
     * command name 뒤에는 문자열 끝이거나 공백이 와야 한다.
     *
     * 예:
     *   "mem"      -> mem 명령 OK
     *   "mem   "   -> mem 명령 OK
     *   "memory"   -> mem 명령 아님
     */
    if (*p != '\0' && *p != ' ') {
        return 0;
    }

    p = command_skip_spaces(p);

    if (args_out) {
        *args_out = p;
    }

    return 1;
}

static command_t* command_find_by_name(const char* name) {
    command_t* current = command_head;

    while (current) {
        if (command_streq(current->name, name)) {
            return current;
        }

        current = current->next;
    }

    return 0;
}

void command_init(void) {
    command_head = 0;
    command_tail = 0;
    command_count = 0;

    print_color("Command registry initialized\n", COLOR_GREEN_ON_BLACK);
}

u32 command_register(
    const char* name,
    const char* description,
    command_handler_t handler
) {
    if (!name || !description || !handler) {
        return 0;
    }

    if (command_find_by_name(name)) {
        print_color("command_register duplicate ignored: ", COLOR_RED_ON_BLACK);
        print(name);
        print("\n");
        return 0;
    }

    command_t* command = (command_t*)kzalloc(sizeof(command_t));

    if (!command) {
        print_color("command_register failed: no memory\n", COLOR_RED_ON_BLACK);
        return 0;
    }

    command->name = command_strdup(name);
    command->description = command_strdup(description);
    command->handler = handler;
    command->next = 0;

    if (!command->name || !command->description) {
        /*
         * 지금 heap에는 완전한 reclaim/debug용 path가 없으니
         * 단순히 실패만 알리고 반환한다.
         * 일반 상황에서는 여기까지 올 가능성은 낮다.
         */
        print_color("command_register failed: string alloc failed\n", COLOR_RED_ON_BLACK);
        return 0;
    }

    if (!command_head) {
        command_head = command;
        command_tail = command;
    } else {
        command_tail->next = command;
        command_tail = command;
    }

    command_count++;

    return 1;
}

void command_print_help(void) {
    command_t* current = command_head;

    print("commands:\n");

    while (current) {
        print("  ");
        print(current->name);

        /*
         * 간단한 정렬용 공백.
         * command name이 길면 그냥 붙어서 나와도 기능에는 문제 없음.
         */
        u64 name_len = command_strlen(current->name);

        if (name_len < 9) {
            for (u64 i = name_len; i < 9; i++) {
                print(" ");
            }
        } else {
            print(" ");
        }

        print("- ");
        print(current->description);
        print("\n");

        current = current->next;
    }
}

u32 command_execute(const char* line) {
    if (!line) {
        return 0;
    }

    line = command_skip_spaces(line);

    if (*line == '\0') {
        return 1;
    }

    command_t* current = command_head;

    while (current) {
        const char* args = 0;

        if (command_token_matches(line, current->name, &args)) {
            current->handler(args);
            return 1;
        }

        current = current->next;
    }

    print("unknown command: ");
    print(line);
    print("\n");
    print("type 'help'\n");

    return 0;
}

/*
 * Built-in commands
 */

static void cmd_help(const char* args) {
    (void)args;

    command_print_help();
}

static void cmd_clear(const char* args) {
    (void)args;

    console_clear();
}

static void cmd_ticks(const char* args) {
    (void)args;

    print("ticks = ");
    print_dec64(pit_get_ticks());
    print("\n");
}

static void cmd_mem(const char* args) {
    (void)args;

    print("free frames = ");
    print_dec64(pmm_free_frame_count());
    print("\n");

    print("free bytes  = ");
    print_dec64(pmm_free_frame_count() * PAGE_SIZE);
    print("\n");
}

static void cmd_echo(const char* args) {
    print(args);
    print("\n");
}

static void cmd_pmmtest(const char* args) {
    (void)args;

    pmm_test();
}

static void cmd_vmmtest(const char* args) {
    (void)args;

    vmm_test_mapping();
    print("\n");
    vmm_test_unmap();
}

static void cmd_heaptest(const char* args) {
    (void)args;

    heap_test();
}

static void cmd_pf(const char* args) {
    (void)args;

    print("triggering page fault...\n");
    idt_test_page_fault();
}

static void cmd_cmds(const char* args) {
    (void)args;

    print("registered commands = ");
    print_dec64(command_count);
    print("\n");
}

void command_register_builtin_commands(void) {
    command_register("help", "show command list", cmd_help);
    command_register("clear", "clear screen", cmd_clear);
    command_register("ticks", "show PIT timer ticks", cmd_ticks);
    command_register("mem", "show free physical frames", cmd_mem);
    command_register("echo", "print arguments", cmd_echo);
    command_register("pmmtest", "run physical memory manager test", cmd_pmmtest);
    command_register("vmmtest", "run virtual memory manager tests", cmd_vmmtest);
    command_register("heaptest", "run kernel heap allocator test", cmd_heaptest);
    command_register("pf", "trigger page fault test", cmd_pf);
    command_register("cmds", "show registered command count", cmd_cmds);

    print("registered commands = ");
    print_dec64(command_count);
    print("\n");
}