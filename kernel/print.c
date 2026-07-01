#include "console.h"
#include "klog.h"
#include "print.h"
#include "serial.h"
#include "types.h"

static void print_char(char c) {
    console_put_char(c);
    serial_write_char(c);
    klog_write_char(c);
}

void print(const char* s) {
    while (*s) {
        print_char(*s);
        s++;
    }
}

void print_color(const char* s, u8 color) {
    u8 old_color = console_get_color();

    console_set_color(color);
    print(s);
    console_set_color(old_color);
}

void print_hex32(u32 value) {
    const char* hex = "0123456789ABCDEF";

    print("0x");

    for (int shift = 28; shift >= 0; shift -= 4) {
        print_char(hex[(value >> shift) & 0xF]);
    }
}

void print_hex64(u64 value) {
    const char* hex = "0123456789ABCDEF";

    print("0x");

    for (int shift = 60; shift >= 0; shift -= 4) {
        print_char(hex[(value >> shift) & 0xF]);
    }
}

void print_dec64(u64 value) {
    char buffer[21];
    int index = 0;

    if (value == 0) {
        print_char('0');
        return;
    }

    while (value > 0) {
        buffer[index] = '0' + (value % 10);
        value = value / 10;
        index++;
    }

    while (index > 0) {
        index--;
        print_char(buffer[index]);
    }
}