#include "command.h"
#include "console.h"
#include "device.h"
#include "io.h"
#include "irq.h"
#include "mouse.h"
#include "pic.h"
#include "print.h"
#include "types.h"

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64

#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL  0x02

#define PS2_CMD_READ_CONFIG    0x20
#define PS2_CMD_WRITE_CONFIG   0x60
#define PS2_CMD_ENABLE_AUX     0xA8
#define PS2_CMD_WRITE_AUX      0xD4

#define MOUSE_ACK              0xFA

#define MOUSE_CMD_SET_DEFAULTS      0xF6
#define MOUSE_CMD_ENABLE_REPORTING  0xF4
#define MOUSE_CMD_SET_SAMPLE_RATE   0xF3
#define MOUSE_CMD_GET_DEVICE_ID     0xF2

static device_t* mouse_device = 0;

static u8 mouse_packet[4];
static u32 mouse_packet_index = 0;
static u32 mouse_packet_size = 3;
static u32 mouse_has_wheel = 0;

static u32 packets_received = 0;
static u32 wheel_events = 0;
static s32 wheel_position = 0;

static void ps2_wait_input_empty(void) {
    for (u32 i = 0; i < 100000; i++) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0) {
            return;
        }
    }
}

static u32 ps2_wait_output_full(void) {
    for (u32 i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            return 1;
        }
    }

    return 0;
}

static void ps2_write_command(u8 command) {
    ps2_wait_input_empty();
    outb(PS2_COMMAND_PORT, command);
}

static void ps2_write_data(u8 data) {
    ps2_wait_input_empty();
    outb(PS2_DATA_PORT, data);
}

static u8 ps2_read_data(void) {
    if (!ps2_wait_output_full()) {
        return 0;
    }

    return inb(PS2_DATA_PORT);
}

static void mouse_write(u8 value) {
    ps2_write_command(PS2_CMD_WRITE_AUX);
    ps2_write_data(value);
}

static u8 mouse_read_ack(void) {
    return ps2_read_data();
}

static u32 mouse_send_command(u8 command) {
    mouse_write(command);

    return mouse_read_ack() == MOUSE_ACK;
}

static u32 mouse_set_sample_rate(u8 rate) {
    if (!mouse_send_command(MOUSE_CMD_SET_SAMPLE_RATE)) {
        return 0;
    }

    mouse_write(rate);

    return mouse_read_ack() == MOUSE_ACK;
}

static u8 mouse_get_device_id(void) {
    if (!mouse_send_command(MOUSE_CMD_GET_DEVICE_ID)) {
        return 0;
    }

    return ps2_read_data();
}

static void mouse_try_enable_wheel(void) {
    /*
     * IntelliMouse wheel detection sequence:
     *   set sample rate 200
     *   set sample rate 100
     *   set sample rate 80
     *   get device id
     *
     * device id 3이면 wheel mouse.
     */
    mouse_set_sample_rate(200);
    mouse_set_sample_rate(100);
    mouse_set_sample_rate(80);

    u8 id = mouse_get_device_id();

    if (id == 3) {
        mouse_has_wheel = 1;
        mouse_packet_size = 4;
    } else {
        mouse_has_wheel = 0;
        mouse_packet_size = 3;
    }
}

static s8 mouse_decode_wheel(u8 raw) {
    /*
     * IntelliMouse 4th packet byte:
     *   low 4 bits = signed wheel delta
     *
     * 0x01 = +1
     * 0x0F = -1
     */
    s8 value = (s8)(raw & 0x0F);

    if (value & 0x08) {
        value = (s8)(value | 0xF0);
    }

    return value;
}

static void print_s32(s32 value) {
    if (value < 0) {
        print("-");
        print_dec64((u64)(-value));
        return;
    }

    print_dec64((u64)value);
}

static void mouse_handle_packet(void) {
    packets_received++;

    /*
     * byte0 bit3는 항상 1이어야 한다.
     * 동기화가 깨진 packet이면 버린다.
     */
    if ((mouse_packet[0] & 0x08) == 0) {
        return;
    }

    if (!mouse_has_wheel || mouse_packet_size < 4) {
        return;
    }

    s8 wheel = mouse_decode_wheel(mouse_packet[3]);

    if (wheel == 0) {
        return;
    }

    wheel_position += wheel;
    wheel_events++;

    /*
     * Phase 7 preparation:
     *   macOS 자연 스크롤 감각에 맞춰 기존 방향을 반대로 둔다.
     *
     * 이전:
     *   wheel > 0 -> line up
     *   wheel < 0 -> line down
     *
     * 현재:
     *   wheel > 0 -> line down
     *   wheel < 0 -> line up
     */
    if (wheel > 0) {
        console_scroll_line_down();
    } else {
        console_scroll_line_up();
    }
}

void mouse_init(void) {
    mouse_packet_index = 0;
    mouse_packet_size = 3;
    mouse_has_wheel = 0;

    packets_received = 0;
    wheel_events = 0;
    wheel_position = 0;

    ps2_write_command(PS2_CMD_ENABLE_AUX);

    ps2_write_command(PS2_CMD_READ_CONFIG);
    u8 config = ps2_read_data();

    config = (u8)(config | 0x02);
    config = (u8)(config & ~0x20);

    ps2_write_command(PS2_CMD_WRITE_CONFIG);
    ps2_write_data(config);

    mouse_send_command(MOUSE_CMD_SET_DEFAULTS);
    mouse_try_enable_wheel();
    mouse_send_command(MOUSE_CMD_ENABLE_REPORTING);

    irq_register_handler(12, mouse_on_irq);

    pic_clear_mask(2);
    pic_clear_mask(12);

    mouse_device = device_register(
        "mouse0",
        DEVICE_TYPE_INPUT,
        DEVICE_STATE_READY,
        mouse_has_wheel ? "ps2-wheel-mouse" : "ps2-mouse"
    );

    (void)mouse_device;

    if (mouse_has_wheel) {
        print_color("PS/2 mouse initialized with wheel support\n", COLOR_GREEN_ON_BLACK);
    } else {
        print_color("PS/2 mouse initialized without wheel support\n", COLOR_YELLOW_ON_BLACK);
    }
}

void mouse_on_irq(void) {
    u8 status = inb(PS2_STATUS_PORT);

    if ((status & PS2_STATUS_OUTPUT_FULL) == 0) {
        return;
    }

    u8 data = inb(PS2_DATA_PORT);

    if (mouse_packet_index == 0 && (data & 0x08) == 0) {
        return;
    }

    mouse_packet[mouse_packet_index] = data;
    mouse_packet_index++;

    if (mouse_packet_index >= mouse_packet_size) {
        mouse_packet_index = 0;
        mouse_handle_packet();
    }
}

static void cmd_mouseinfo(const char* args) {
    (void)args;

    print("mouse packet size = ");
    print_dec64(mouse_packet_size);
    print("\n");

    print("mouse has wheel = ");
    print_dec64(mouse_has_wheel);
    print("\n");

    print("mouse packets = ");
    print_dec64(packets_received);
    print("\n");

    print("mouse wheel events = ");
    print_dec64(wheel_events);
    print("\n");

    print("mouse wheel position = ");
    print_s32(wheel_position);
    print("\n");

    print("mouse scroll direction = natural\n");
}

void mouse_register_builtin_commands(void) {
    command_register("mouseinfo", "show PS/2 mouse stats", cmd_mouseinfo);
}
