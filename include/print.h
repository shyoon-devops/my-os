#ifndef GO_OS_PRINT_H
#define GO_OS_PRINT_H

#include "types.h"

void print(const char* s);
void print_color(const char* s, u8 color);
void print_hex32(u32 value);
void print_hex64(u64 value);
void print_dec64(u64 value);

#endif
