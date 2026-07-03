#include "device.h"
#include "io.h"
#include "print.h"
#include "serial.h"
#include "types.h"

#define SERIAL_COM1_PORT 0x3F8

#define SERIAL_DATA_PORT(base)           ((base) + 0)
#define SERIAL_INTERRUPT_ENABLE(base)    ((base) + 1)
#define SERIAL_FIFO_CONTROL(base)        ((base) + 2)
#define SERIAL_LINE_CONTROL(base)        ((base) + 3)
#define SERIAL_MODEM_CONTROL(base)       ((base) + 4)
#define SERIAL_LINE_STATUS(base)         ((base) + 5)

#define SERIAL_LINE_STATUS_TRANSMIT_EMPTY 0x20

static u32 serial_initialized = 0;
static device_t* serial_device = 0;

static u32 serial_transmit_empty(void) {
    return inb(SERIAL_LINE_STATUS(SERIAL_COM1_PORT)) &
           SERIAL_LINE_STATUS_TRANSMIT_EMPTY;
}

static void serial_write_raw_char(char c) {
    /*
     * UART가 준비될 때까지 기다린다.
     * 무한 루프로 걸리지 않도록 timeout을 둔다.
     */
    for (u32 i = 0; i < 100000; i++) {
        if (serial_transmit_empty()) {
            outb(SERIAL_DATA_PORT(SERIAL_COM1_PORT), (u8)c);
            return;
        }
    }
}

void serial_init(void) {
    /*
     * COM1 16550 UART 초기화.
     *
     * baud rate divisor = 3
     * base clock 115200 / 3 = 38400 baud
     */

    /*
     * Interrupt disable.
     */
    outb(SERIAL_INTERRUPT_ENABLE(SERIAL_COM1_PORT), 0x00);

    /*
     * DLAB enable.
     */
    outb(SERIAL_LINE_CONTROL(SERIAL_COM1_PORT), 0x80);

    /*
     * Divisor low/high.
     */
    outb(SERIAL_DATA_PORT(SERIAL_COM1_PORT), 0x03);
    outb(SERIAL_INTERRUPT_ENABLE(SERIAL_COM1_PORT), 0x00);

    /*
     * 8 bits, no parity, one stop bit.
     */
    outb(SERIAL_LINE_CONTROL(SERIAL_COM1_PORT), 0x03);

    /*
     * Enable FIFO, clear RX/TX queues, 14-byte threshold.
     */
    outb(SERIAL_FIFO_CONTROL(SERIAL_COM1_PORT), 0xC7);

    /*
     * IRQ disabled at IER, but set DTR/RTS/OUT2.
     */
    outb(SERIAL_MODEM_CONTROL(SERIAL_COM1_PORT), 0x0B);

    serial_initialized = 1;

    print("Serial COM1 initialized\n");
}

void serial_register_device(void) {
    if (serial_initialized) {
        serial_device = device_register(
            "serial0",
            DEVICE_TYPE_TTY,
            DEVICE_STATE_READY,
            "uart-16550"
        );
    } else {
        serial_device = device_register(
            "serial0",
            DEVICE_TYPE_TTY,
            DEVICE_STATE_FAILED,
            "uart-16550"
        );
    }

    (void)serial_device;
}

void serial_write_char(char c) {
    if (!serial_initialized) {
        return;
    }

    /*
     * 터미널 호환성을 위해 '\n' 앞에 '\r'도 보낸다.
     */
    if (c == '\n') {
        serial_write_raw_char('\r');
    }

    serial_write_raw_char(c);
}