#ifndef GO_OS_RTC_H
#define GO_OS_RTC_H

#include "types.h"

typedef struct rtc_time {
    u32 year;
    u8 month;
    u8 day;

    u8 hour;
    u8 minute;
    u8 second;
} rtc_time_t;

void rtc_init(void);

u32 rtc_read_time(rtc_time_t* out);
void rtc_print_time(const rtc_time_t* t);

void rtc_register_builtin_commands(void);

#endif
