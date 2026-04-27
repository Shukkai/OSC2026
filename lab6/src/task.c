/* src/task.c */
#include "task.h"
#include "mm.h"
#include "trap.h" 
#include "printk.h"
#define TASK_PRIO_IDLE  0x7FFFFFFF
static struct list_head task_queue;
static int current_task_priority = TASK_PRIO_IDLE;
void task_init() {
    INIT_LIST_HEAD(&task_queue);
    current_task_priority = TASK_PRIO_IDLE;
}

void task_add(task_func_t func, void *arg, int priority) {
    struct task *t = (struct task *)kmalloc(sizeof(struct task));
    if (!t) return;

    t->func = func;
    t->arg = arg;
    t->priority = priority;
    t->running = 0;  // no longer used for preemption, but harmless to keep
    INIT_LIST_HEAD(&t->list);

    // 1. Save current interrupt state
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));

    // 2. Disable interrupts — list modification is a critical section
    disable_interrupt();

    // 3. Sorted insert (lower priority number = higher priority = earlier in list)
    struct list_head *pos;
    struct task *curr;
    int inserted = 0;

    list_for_each(pos, &task_queue) {
        curr = list_entry(pos, struct task, list);
        if (curr->priority > t->priority) {
            list_add_tail(&t->list, pos);  // insert BEFORE curr
            inserted = 1;
            break;
        }
    }

    if (!inserted) {
        list_add_tail(&t->list, &task_queue);
    }

    // 4. Restore interrupt state
    if (sstatus & SSTATUS_SIE) {
        enable_interrupt();
    }

    // 5. Preemption trigger:
    //    If the task we just added outranks whatever is currently executing,
    //    re-enter task_run() so it preempts the current task on the C stack.
    //
    //    Only do this if interrupts were ON coming in — i.e. we're in a
    //    context where running tasks is actually allowed. If we were called
    //    from deep inside an ISR with interrupts forcibly off, the outer
    //    trap-return path's task_run() call will handle it.
    if ((sstatus & SSTATUS_SIE) && priority < current_task_priority) {
        task_run();
    }
}

void task_run(void) {
    while (1) {
        disable_interrupt();

        if (list_empty(&task_queue)) {
            enable_interrupt();
            return;
        }

        struct task *t = list_first_entry(&task_queue, struct task, list);

        // Preemption check: only run a task if it's strictly higher priority
        // than whatever's currently executing. (Lower number = higher priority
        // in your convention.)
        if (t->priority >= current_task_priority) {
            enable_interrupt();
            return;
        }

        // Remove from queue BEFORE running — simpler than the `running` flag
        list_del(&t->list);

        int saved_prio = current_task_priority;
        current_task_priority = t->priority;

        enable_interrupt();
        t->func(t->arg);
        disable_interrupt();

        current_task_priority = saved_prio;
        kfree(t);

        enable_interrupt();
    }
}
