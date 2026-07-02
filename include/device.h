#ifndef MY_OS_DEVICE_H
#define MY_OS_DEVICE_H

#include "types.h"

typedef enum {
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_DISPLAY,
    DEVICE_TYPE_INPUT,
    DEVICE_TYPE_TIMER,
    DEVICE_TYPE_TTY,
    DEVICE_TYPE_STORAGE,
    DEVICE_TYPE_NETWORK,
    DEVICE_TYPE_VIRTUAL
} device_type_t;

typedef enum {
    DEVICE_STATE_UNKNOWN = 0,
    DEVICE_STATE_REGISTERED,
    DEVICE_STATE_READY,
    DEVICE_STATE_FAILED
} device_state_t;

typedef struct device {
    u32 id;

    char* name;
    char* driver;

    device_type_t type;
    device_state_t state;

    struct device* next;
} device_t;

void device_init(void);

device_t* device_register(
    const char* name,
    device_type_t type,
    device_state_t state,
    const char* driver
);

device_t* device_find(const char* name);

void device_set_state(device_t* dev, device_state_t state);

u32 device_count(void);
void device_print_all(void);

void device_register_builtin_commands(void);

const char* device_type_name(device_type_t type);
const char* device_state_name(device_state_t state);

#endif