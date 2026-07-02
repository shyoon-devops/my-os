/*
 * Tiny userland placeholder.
 *
 * Phase 8에서는 이 ELF를 실제 실행하지 않는다.
 * 커널 ELF loader가 /bin/init을 읽고 PT_LOAD segment를 메모리에
 * 적재할 수 있는지 확인하기 위한 테스트용 바이너리다.
 */

volatile unsigned long user_init_magic = 0x1234ABCDUL;

void _start(void) {
    for (;;) {
        user_init_magic++;
    }
}
