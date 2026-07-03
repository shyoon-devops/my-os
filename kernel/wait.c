#include "spinlock.h"
#include "task.h"
#include "types.h"
#include "wait.h"

void wait_queue_init(wait_queue_t* queue) {
    if (!queue) {
        return;
    }

    queue->head = 0;
    queue->tail = 0;

    spinlock_init(&queue->lock);
}

void wait_queue_block_irqrestore(wait_queue_t* queue, u64 irq_flags) {
    if (!queue) {
        irq_restore(irq_flags);
        return;
    }

    struct task* current = task_current();

    if (!current) {
        irq_restore(irq_flags);
        return;
    }

    /*
     * 여기까지 caller가 IRQ를 끈 상태로 들어온다.
     *
     * 이유:
     *   "조건 확인 → wait queue 등록" 사이에 IRQ가 끼어들면
     *   wakeup을 놓칠 수 있기 때문이다.
     */
    u64 lock_flags;

    spin_lock_irqsave(&queue->lock, &lock_flags);

    task_wait_set_next(current, 0);

    if (!queue->head) {
        queue->head = current;
        queue->tail = current;
    } else {
        task_wait_set_next(queue->tail, current);
        queue->tail = current;
    }

    task_set_waiting(current);

    spin_unlock_irqrestore(&queue->lock, lock_flags);

    /*
     * 이제 wait queue에 등록됐으므로 IRQ를 다시 켜도 된다.
     *
     * 이 직후 키보드 IRQ가 들어와서 이 task를 READY로 깨워도 괜찮다.
     * 그 경우 아래 task_block_switch()로 scheduler에 돌아간 뒤,
     * 다음 scheduling 때 곧바로 다시 실행된다.
     */
    irq_restore(irq_flags);

    task_block_switch();
}

u32 wait_queue_wake_one(wait_queue_t* queue) {
    if (!queue) {
        return 0;
    }

    u64 flags;
    u32 woke = 0;

    spin_lock_irqsave(&queue->lock, &flags);

    struct task* task = queue->head;

    if (task) {
        queue->head = task_wait_next(task);

        if (!queue->head) {
            queue->tail = 0;
        }

        task_wait_set_next(task, 0);
        task_set_ready(task);

        woke = 1;
    }

    spin_unlock_irqrestore(&queue->lock, flags);

    return woke;
}

u32 wait_queue_wake_all(wait_queue_t* queue) {
    if (!queue) {
        return 0;
    }

    u64 flags;
    u32 woke = 0;

    spin_lock_irqsave(&queue->lock, &flags);

    struct task* task = queue->head;

    while (task) {
        struct task* next = task_wait_next(task);

        task_wait_set_next(task, 0);
        task_set_ready(task);

        woke++;

        task = next;
    }

    queue->head = 0;
    queue->tail = 0;

    spin_unlock_irqrestore(&queue->lock, flags);

    return woke;
}
