#include "console.h"
#include "io.h"
#include "pic.h"
#include "print.h"
#include "types.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define PIC_EOI      0x20

#define ICW1_INIT    0x10
#define ICW1_ICW4    0x01
#define ICW4_8086    0x01

static void pic_remap(u8 master_offset, u8 slave_offset) {
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, master_offset);
    io_wait();

    outb(PIC2_DATA, slave_offset);
    io_wait();

    /*
     * master PIC의 IRQ2에 slave PIC가 연결되어 있다는 뜻.
     */
    outb(PIC1_DATA, 0x04);
    io_wait();

    /*
     * slave PIC는 master의 IRQ2 뒤에 붙어 있다는 뜻.
     */
    outb(PIC2_DATA, 0x02);
    io_wait();

    outb(PIC1_DATA, ICW4_8086);
    io_wait();

    outb(PIC2_DATA, ICW4_8086);
    io_wait();
}

void pic_init(void) {
    pic_remap(PIC_REMAP_OFFSET_MASTER, PIC_REMAP_OFFSET_SLAVE);

    /*
     * 일단 모든 IRQ를 막는다.
     * 각 드라이버가 init 단계에서 필요한 IRQ만 직접 연다.
     */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    print_color("PIC remapped and all IRQs masked\n", COLOR_GREEN_ON_BLACK);
}

void pic_send_eoi(u8 irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }

    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_set_mask(u8 irq) {
    u16 port;
    u8 value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq = irq - 8;
    }

    value = inb(port);
    value = value | (u8)(1u << irq);
    outb(port, value);
}

void pic_clear_mask(u8 irq) {
    u16 port;
    u8 value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq = irq - 8;
    }

    value = inb(port);
    value = value & (u8)~(1u << irq);
    outb(port, value);
}