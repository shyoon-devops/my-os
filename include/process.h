#ifndef MY_OS_PROCESS_H
#define MY_OS_PROCESS_H

#include "types.h"

/*
 * process_t:
 *   user program 실행 단위. kernel scheduling 단위인 task와 분리한다.
 *
 *   task_t    = "커널이 스케줄하는 실행 흐름" (kernel stack + context)
 *   process_t = "user program 하나의 생명주기" (pid, exit code, 부모 관계)
 *
 * 지금은 process 1개 = user-elf task 1개다.
 * per-process address space는 M3에서 붙는다.
 */

typedef enum {
    PROCESS_STATE_UNUSED = 0,
    PROCESS_STATE_RUNNING,  /* user-elf task가 실행/대기 중 */
    PROCESS_STATE_ZOMBIE,   /* 종료됨, exit code 아직 회수 안 됨 */
    PROCESS_STATE_REAPED    /* wait로 exit code 회수 완료 */
} process_state_t;

/*
 * process_wait() 반환값
 */
#define PROCESS_WAIT_OK          0
#define PROCESS_WAIT_NO_PROCESS  (-1)
#define PROCESS_WAIT_LOAD_FAILED (-2)

void process_init(void);

/*
 * path의 user ELF를 새 process로 실행한다.
 * 내부에서 전용 user-elf task를 만들고 그 안에서 ELF를 로드/실행한다.
 * 반환: pid (실패 시 0)
 */
u32 process_create(const char* path);

/*
 * 현재 task에 연결된 process의 pid. 연결된 process가 없으면 0.
 * (kernel task에서 호출하면 0)
 */
u32 process_current_pid(void);

/*
 * 현재 process를 종료 상태로 기록한다.
 * user task 경로에서는 내부적으로 처리되므로,
 * 이 함수는 커널 코드가 명시적으로 process를 끝낼 때 쓴다.
 */
void process_exit(u64 exit_code);

/*
 * pid가 끝날 때까지 대기하고 exit code를 회수한다.
 * 이미 ZOMBIE/REAPED면 즉시 반환한다 (exit status 조회 역할).
 *
 * 반환:
 *   PROCESS_WAIT_OK          - 정상 종료, *out_exit_code 유효
 *   PROCESS_WAIT_NO_PROCESS  - 해당 pid 없음
 *   PROCESS_WAIT_LOAD_FAILED - ELF 로드/실행 실패
 */
s32 process_wait(u32 pid, u64* out_exit_code);

const char* process_state_name(process_state_t state);

void process_register_builtin_commands(void);

#endif
