#ifndef MY_OS_SERIAL_H
#define MY_OS_SERIAL_H

#include "types.h"

void serial_init(void);
void serial_register_device(void);

void serial_write_char(char c);

#endif