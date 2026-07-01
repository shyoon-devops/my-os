#ifndef GO_OS_SERIAL_H
#define GO_OS_SERIAL_H

#include "types.h"

void serial_init(void);
void serial_register_device(void);

u32 serial_is_initialized(void);

void serial_write_char(char c);
void serial_write_string(const char* s);

#endif