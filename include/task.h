#ifndef MY_OS_TASK_H
#define MY_OS_TASK_H

#include "types.h"

typedef void (*task_entry_t)(void* arg);

/*
 * task별 ring3 실행 컨텍스트.
 *
 * ring3_enter()가 진입 직전의 커널 rsp를 여기에 저장하고,
 * SYS_exit이 이 값을 읽어 해당 task의 커널 컨텍스트로 복귀한다.
 * 전역 하나로 저장하면 user task가 여러 개일 때 서로 덮어쓴다.
 */
typedef struct task_user_context {
    u64 saved_kernel_rsp;
} task_user_context_t;

typedef enum {
    TASK_STATE_UNUSED = 0,
    TASK_STATE_READY,
    TASK_STATE_RUNNING,
    TASK_STATE_SLEEPING,
    TASK_STATE_WAITING,
    TASK_STATE_DONE
} task_state_t;

void task_init(void);

u32 task_create(const char* name, task_entry_t entry, void* arg);

u32 task_run_once(void);

void task_yield(void);
void task_sleep_ticks(u64 ticks);
void task_exit(void);

u32 task_count(void);
u32 task_ready_count(void);
u32 task_sleeping_count(void);
u32 task_waiting_count(void);

void task_print_all(void);
void task_register_builtin_commands(void);

const char* task_state_name(task_state_t state);

/*
 * wait queue가 사용하는 최소 task primitive.
 *
 * task 구조체 본문은 task.c 안에 숨겨두고,
 * wait queue는 아래 함수들만 통해 task를 조작한다.
 */
struct task;

struct task* task_current(void);

/*
 * 현재 task의 ring3 컨텍스트. current task가 없으면 0.
 */
task_user_context_t* task_current_user_context(void);

/*
 * syscall_entry.asm의 SYS_exit 경로에서 호출된다.
 * 현재 task의 saved_kernel_rsp 값을 돌려준다 (없으면 0).
 */
u64 task_user_saved_kernel_rsp(void);

void task_set_waiting(struct task* task);
void task_set_ready(struct task* task);

struct task* task_wait_next(struct task* task);
void task_wait_set_next(struct task* task, struct task* next);

/*
 * 현재 task의 상태는 이미 바뀌었다고 가정하고,
 * scheduler context로 전환한다.
 */
void task_block_switch(void);

#endif
