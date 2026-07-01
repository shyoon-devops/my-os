#ifndef GO_OS_PIC_H
#define GO_OS_PIC_H

#include "types.h"

#define PIC_REMAP_OFFSET_MASTER 0x20
#define PIC_REMAP_OFFSET_SLAVE  0x28

void pic_init(void);
void pic_send_eoi(u8 irq);
void pic_set_mask(u8 irq);
void pic_clear_mask(u8 irq);

#endif