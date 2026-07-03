#ifndef MY_OS_WAIT_H
#define MY_OS_WAIT_H

#include "spinlock.h"
#include "types.h"

struct task;

/*
 * wait_queue_t:
 *   어떤 이벤트를 기다리는 task들의 큐.
 *
 * 예:
 *   tty input wait queue
 *   socket receive wait queue
 *   child process wait queue
 *
 * 지금은 단일 CPU + cooperative scheduler 기준의 최소 구현이다.
 */
typedef struct wait_queue {
    struct task* head;
    struct task* tail;
    spinlock_t lock;
} wait_queue_t;

void wait_queue_init(wait_queue_t* queue);

/*
 * 현재 task를 queue에 넣고 WAITING 상태로 만든 뒤 scheduler로 전환한다.
 *
 * irq_flags는 호출자가 irq_save()로 저장한 값이다.
 * 이 함수는 wait queue 등록이 끝난 뒤 irq_restore(irq_flags)를 수행한다.
 */
void wait_queue_block_irqrestore(wait_queue_t* queue, u64 irq_flags);

u32 wait_queue_wake_one(wait_queue_t* queue);
u32 wait_queue_wake_all(wait_queue_t* queue);

#endif
