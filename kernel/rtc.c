#include "command.h"
#include "console.h"
#include "device.h"
#include "io.h"
#include "print.h"
#include "rtc.h"
#include "spinlock.h"
#include "types.h"

#define CMOS_ADDRESS_PORT 0x70
#define CMOS_DATA_PORT    0x71

#define CMOS_REG_SECOND   0x00
#define CMOS_REG_MINUTE   0x02
#define CMOS_REG_HOUR     0x04
#define CMOS_REG_DAY      0x07
#define CMOS_REG_MONTH    0x08
#define CMOS_REG_YEAR     0x09
#define CMOS_REG_STATUS_A 0x0A
#define CMOS_REG_STATUS_B 0x0B

#define CMOS_NMI_DISABLE  0x80

static spinlock_t rtc_lock;
static device_t* rtc_device = 0;

static u8 cmos_read(u8 reg) {
    /*
     * bit 7을 1로 세워 NMI를 잠깐 disable한 상태에서 CMOS register를 고른다.
     */
    outb(CMOS_ADDRESS_PORT, CMOS_NMI_DISABLE | reg);
    return inb(CMOS_DATA_PORT);
}

static u32 rtc_update_in_progress(void) {
    return (cmos_read(CMOS_REG_STATUS_A) & 0x80) != 0;
}

static u8 bcd_to_bin(u8 value) {
    return (u8)((value & 0x0F) + ((value >> 4) * 10));
}

static void print_2digits(u32 value) {
    console_put_char((char)('0' + ((value / 10) % 10)));
    console_put_char((char)('0' + (value % 10)));
}

static void print_4digits(u32 value) {
    console_put_char((char)('0' + ((value / 1000) % 10)));
    console_put_char((char)('0' + ((value / 100) % 10)));
    console_put_char((char)('0' + ((value / 10) % 10)));
    console_put_char((char)('0' + (value % 10)));
}

void rtc_init(void) {
    spinlock_init(&rtc_lock);

    rtc_device = device_register(
        "rtc0",
        DEVICE_TYPE_TIMER,
        DEVICE_STATE_READY,
        "cmos-rtc"
    );

    (void)rtc_device;

    print_color("CMOS RTC initialized\n", COLOR_GREEN_ON_BLACK);
}

u32 rtc_read_time(rtc_time_t* out) {
    if (!out) {
        return 0;
    }

    u64 flags;
    spin_lock_irqsave(&rtc_lock, &flags);

    /*
     * RTC가 시간을 갱신하는 중이면 값이 찢길 수 있다.
     * update-in-progress가 끝날 때까지 잠깐 기다린다.
     */
    for (u32 i = 0; i < 100000; i++) {
        if (!rtc_update_in_progress()) {
            break;
        }
    }

    u8 second = cmos_read(CMOS_REG_SECOND);
    u8 minute = cmos_read(CMOS_REG_MINUTE);
    u8 hour = cmos_read(CMOS_REG_HOUR);
    u8 day = cmos_read(CMOS_REG_DAY);
    u8 month = cmos_read(CMOS_REG_MONTH);
    u8 year = cmos_read(CMOS_REG_YEAR);
    u8 status_b = cmos_read(CMOS_REG_STATUS_B);

    /*
     * status B bit 2:
     *   0 = BCD
     *   1 = binary
     */
    if ((status_b & 0x04) == 0) {
        second = bcd_to_bin(second);
        minute = bcd_to_bin(minute);
        hour = bcd_to_bin((u8)(hour & 0x7F));
        day = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year = bcd_to_bin(year);
    }

    /*
     * status B bit 1:
     *   0 = 12-hour mode
     *   1 = 24-hour mode
     *
     * 12-hour mode에서 hour bit 7이 PM 표시.
     */
    if ((status_b & 0x02) == 0) {
        u8 pm = hour & 0x80;
        hour = hour & 0x7F;

        if (pm && hour < 12) {
            hour += 12;
        }

        if (!pm && hour == 12) {
            hour = 0;
        }
    }

    /*
     * CMOS year는 보통 00~99라서 일단 2000년대라고 본다.
     * QEMU/현대 환경 기준으로 학습용 OS에서는 이 정도로 충분함.
     */
    out->year = 2000 + year;
    out->month = month;
    out->day = day;
    out->hour = hour;
    out->minute = minute;
    out->second = second;

    spin_unlock_irqrestore(&rtc_lock, flags);

    return 1;
}

void rtc_print_time(const rtc_time_t* t) {
    if (!t) {
        print("(null rtc time)\n");
        return;
    }

    print_4digits(t->year);
    console_put_char('-');
    print_2digits(t->month);
    console_put_char('-');
    print_2digits(t->day);

    console_put_char(' ');

    print_2digits(t->hour);
    console_put_char(':');
    print_2digits(t->minute);
    console_put_char(':');
    print_2digits(t->second);
}

static void cmd_date(const char* args) {
    (void)args;

    rtc_time_t now;

    if (!rtc_read_time(&now)) {
        print_color("failed to read RTC\n", COLOR_RED_ON_BLACK);
        return;
    }

    rtc_print_time(&now);
    print("\n");
}

static void cmd_rtctest(const char* args) {
    (void)args;

    rtc_time_t now;

    print_color("[RTC test]\n", COLOR_YELLOW_ON_BLACK);

    if (!rtc_read_time(&now)) {
        print_color("RTC read FAILED\n", COLOR_RED_ON_BLACK);
        return;
    }

    print("current RTC time = ");
    rtc_print_time(&now);
    print("\n");

    if (now.month >= 1 && now.month <= 12 &&
        now.day >= 1 && now.day <= 31 &&
        now.hour <= 23 &&
        now.minute <= 59 &&
        now.second <= 59) {
        print_color("RTC OK\n", COLOR_GREEN_ON_BLACK);
    } else {
        print_color("RTC value looks invalid\n", COLOR_RED_ON_BLACK);
    }
}

void rtc_register_builtin_commands(void) {
    command_register("date", "show CMOS RTC date/time", cmd_date);
    command_register("rtctest", "test CMOS RTC driver", cmd_rtctest);
}
