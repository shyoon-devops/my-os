#include "command.h"
#include "console.h"
#include "device.h"
#include "heap.h"
#include "print.h"
#include "types.h"

static device_t* device_head = 0;
static device_t* device_tail = 0;

static u32 next_device_id = 1;
static u32 registered_device_count = 0;
static u32 device_initialized = 0;

static u64 dev_strlen(const char* s) {
    u64 len = 0;

    if (!s) {
        return 0;
    }

    while (s[len]) {
        len++;
    }

    return len;
}

static u32 dev_streq(const char* a, const char* b) {
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

static char* dev_strdup(const char* s) {
    if (!s) {
        return 0;
    }

    u64 len = dev_strlen(s);
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

const char* device_type_name(device_type_t type) {
    switch (type) {
        case DEVICE_TYPE_DISPLAY:
            return "display";
        case DEVICE_TYPE_INPUT:
            return "input";
        case DEVICE_TYPE_TIMER:
            return "timer";
        case DEVICE_TYPE_TTY:
            return "tty";
        case DEVICE_TYPE_STORAGE:
            return "storage";
        case DEVICE_TYPE_NETWORK:
            return "network";
        case DEVICE_TYPE_VIRTUAL:
            return "virtual";
        default:
            return "unknown";
    }
}

const char* device_state_name(device_state_t state) {
    switch (state) {
        case DEVICE_STATE_REGISTERED:
            return "registered";
        case DEVICE_STATE_READY:
            return "ready";
        case DEVICE_STATE_FAILED:
            return "failed";
        default:
            return "unknown";
    }
}

void device_init(void) {
    device_head = 0;
    device_tail = 0;

    next_device_id = 1;
    registered_device_count = 0;
    device_initialized = 1;

    print_color("Device registry initialized\n", COLOR_GREEN_ON_BLACK);
}

device_t* device_find(const char* name) {
    device_t* current = device_head;

    while (current) {
        if (dev_streq(current->name, name)) {
            return current;
        }

        current = current->next;
    }

    return 0;
}

device_t* device_register(
    const char* name,
    device_type_t type,
    device_state_t state,
    const char* driver
) {
    if (!device_initialized) {
        return 0;
    }

    if (!name || !driver) {
        return 0;
    }

    if (device_find(name)) {
        print_color("device_register duplicate ignored: ", COLOR_RED_ON_BLACK);
        print(name);
        print("\n");
        return 0;
    }

    device_t* dev = (device_t*)kzalloc(sizeof(device_t));

    if (!dev) {
        print_color("device_register failed: no memory\n", COLOR_RED_ON_BLACK);
        return 0;
    }

    dev->name = dev_strdup(name);
    dev->driver = dev_strdup(driver);

    if (!dev->name || !dev->driver) {
        print_color("device_register failed: string alloc failed\n", COLOR_RED_ON_BLACK);
        return 0;
    }

    dev->id = next_device_id;
    next_device_id++;

    dev->type = type;
    dev->state = state;
    dev->next = 0;

    if (!device_head) {
        device_head = dev;
        device_tail = dev;
    } else {
        device_tail->next = dev;
        device_tail = dev;
    }

    registered_device_count++;

    return dev;
}

u32 device_count(void) {
    return registered_device_count;
}

void device_print_all(void) {
    device_t* current = device_head;

    print("devices:\n");

    while (current) {
        print("  #");
        print_dec64(current->id);
        print(" ");

        print(current->name);

        u64 name_len = dev_strlen(current->name);
        if (name_len < 10) {
            for (u64 i = name_len; i < 10; i++) {
                print(" ");
            }
        } else {
            print(" ");
        }

        print(" type=");
        print(device_type_name(current->type));

        print(" state=");
        print(device_state_name(current->state));

        print(" driver=");
        print(current->driver);

        print("\n");

        current = current->next;
    }

    print("device count = ");
    print_dec64(registered_device_count);
    print("\n");
}

static void cmd_devices(const char* args) {
    (void)args;

    device_print_all();
}

static void cmd_devcount(const char* args) {
    (void)args;

    print("device count = ");
    print_dec64(device_count());
    print("\n");
}

void device_register_builtin_commands(void) {
    command_register("devices", "list registered devices", cmd_devices);
    command_register("devcount", "show registered device count", cmd_devcount);
}