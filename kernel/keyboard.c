#include "console.h"
#include "device.h"
#include "io.h"
#include "irq.h"
#include "keyboard.h"
#include "pic.h"
#include "print.h"
#include "tty.h"
#include "types.h"

#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64

#define KEY_RELEASED_MASK 0x80
#define EXTENDED_SCANCODE_PREFIX 0xE0

static u8 shift_pressed = 0;
static u8 extended_scancode = 0;

static device_t* keyboard_device = 0;

static const char scancode_ascii[128] = {
    [0x01] = 27,

    [0x02] = '1',
    [0x03] = '2',
    [0x04] = '3',
    [0x05] = '4',
    [0x06] = '5',
    [0x07] = '6',
    [0x08] = '7',
    [0x09] = '8',
    [0x0A] = '9',
    [0x0B] = '0',
    [0x0C] = '-',
    [0x0D] = '=',
    [0x0E] = '\b',
    [0x0F] = '\t',

    [0x10] = 'q',
    [0x11] = 'w',
    [0x12] = 'e',
    [0x13] = 'r',
    [0x14] = 't',
    [0x15] = 'y',
    [0x16] = 'u',
    [0x17] = 'i',
    [0x18] = 'o',
    [0x19] = 'p',
    [0x1A] = '[',
    [0x1B] = ']',
    [0x1C] = '\n',

    [0x1E] = 'a',
    [0x1F] = 's',
    [0x20] = 'd',
    [0x21] = 'f',
    [0x22] = 'g',
    [0x23] = 'h',
    [0x24] = 'j',
    [0x25] = 'k',
    [0x26] = 'l',
    [0x27] = ';',
    [0x28] = '\'',
    [0x29] = '`',
    [0x2B] = '\\',

    [0x2C] = 'z',
    [0x2D] = 'x',
    [0x2E] = 'c',
    [0x2F] = 'v',
    [0x30] = 'b',
    [0x31] = 'n',
    [0x32] = 'm',
    [0x33] = ',',
    [0x34] = '.',
    [0x35] = '/',

    [0x37] = '*',
    [0x39] = ' ',

    [0x47] = '7',
    [0x48] = '8',
    [0x49] = '9',
    [0x4A] = '-',
    [0x4B] = '4',
    [0x4C] = '5',
    [0x4D] = '6',
    [0x4E] = '+',
    [0x4F] = '1',
    [0x50] = '2',
    [0x51] = '3',
    [0x52] = '0',
    [0x53] = '.',
};

static const char scancode_ascii_shift[128] = {
    [0x01] = 27,

    [0x02] = '!',
    [0x03] = '@',
    [0x04] = '#',
    [0x05] = '$',
    [0x06] = '%',
    [0x07] = '^',
    [0x08] = '&',
    [0x09] = '*',
    [0x0A] = '(',
    [0x0B] = ')',
    [0x0C] = '_',
    [0x0D] = '+',
    [0x0E] = '\b',
    [0x0F] = '\t',

    [0x10] = 'Q',
    [0x11] = 'W',
    [0x12] = 'E',
    [0x13] = 'R',
    [0x14] = 'T',
    [0x15] = 'Y',
    [0x16] = 'U',
    [0x17] = 'I',
    [0x18] = 'O',
    [0x19] = 'P',
    [0x1A] = '{',
    [0x1B] = '}',
    [0x1C] = '\n',

    [0x1E] = 'A',
    [0x1F] = 'S',
    [0x20] = 'D',
    [0x21] = 'F',
    [0x22] = 'G',
    [0x23] = 'H',
    [0x24] = 'J',
    [0x25] = 'K',
    [0x26] = 'L',
    [0x27] = ':',
    [0x28] = '"',
    [0x29] = '~',
    [0x2B] = '|',

    [0x2C] = 'Z',
    [0x2D] = 'X',
    [0x2E] = 'C',
    [0x2F] = 'V',
    [0x30] = 'B',
    [0x31] = 'N',
    [0x32] = 'M',
    [0x33] = '<',
    [0x34] = '>',
    [0x35] = '?',

    [0x37] = '*',
    [0x39] = ' ',

    [0x47] = '7',
    [0x48] = '8',
    [0x49] = '9',
    [0x4A] = '-',
    [0x4B] = '4',
    [0x4C] = '5',
    [0x4D] = '6',
    [0x4E] = '+',
    [0x4F] = '1',
    [0x50] = '2',
    [0x51] = '3',
    [0x52] = '0',
    [0x53] = '.',
};

static char scancode_to_char(u8 scancode) {
    if (scancode >= 128) {
        return 0;
    }

    if (shift_pressed) {
        return scancode_ascii_shift[scancode];
    }

    return scancode_ascii[scancode];
}

static void handle_extended_scancode(u8 scancode) {
    if (scancode & KEY_RELEASED_MASK) {
        return;
    }

    if (scancode == 0x4B) {
        tty_push_key(TTY_KEY_LEFT);
        return;
    }

    if (scancode == 0x4D) {
        tty_push_key(TTY_KEY_RIGHT);
        return;
    }

    if (scancode == 0x47) {
        tty_push_key(TTY_KEY_HOME);
        return;
    }

    if (scancode == 0x4F) {
        tty_push_key(TTY_KEY_END);
        return;
    }
}

void keyboard_init(void) {
    irq_register_handler(1, keyboard_on_irq);
    pic_clear_mask(1);

    keyboard_device = device_register(
        "kbd0",
        DEVICE_TYPE_INPUT,
        DEVICE_STATE_READY,
        "ps2-keyboard"
    );

    print_color("Keyboard initialized and IRQ1 registered\n", COLOR_GREEN_ON_BLACK);
}

void keyboard_on_irq(void) {
    u8 status = inb(KEYBOARD_STATUS_PORT);

    if ((status & 1u) == 0) {
        return;
    }

    u8 scancode = inb(KEYBOARD_DATA_PORT);

    if (scancode == EXTENDED_SCANCODE_PREFIX) {
        extended_scancode = 1;
        return;
    }

    if (extended_scancode) {
        extended_scancode = 0;
        handle_extended_scancode(scancode);
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return;
    }

    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = 0;
        return;
    }

    if (scancode & KEY_RELEASED_MASK) {
        return;
    }

    char c = scancode_to_char(scancode);

    if (c == 0) {
        return;
    }

    if (c == '\b') {
        tty_push_key(TTY_KEY_BACKSPACE);
        return;
    }

    if (c == '\t') {
        tty_push_key(TTY_KEY_TAB);
        return;
    }

    if (c == '\n') {
        tty_push_key(TTY_KEY_ENTER);
        return;
    }

    if (c == 27) {
        tty_push_key(TTY_KEY_ESC);
        return;
    }

    tty_push_char(c);
}