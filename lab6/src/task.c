/* src/task.c */
#include "task.h"
#include "mm.h"
#include "trap.h" 
#include "printk.h"

static struct list_head task_queue;

void task_init() {
    INIT_LIST_HEAD(&task_queue);
}

void task_add(task_func_t func, void *arg, int priority) {
    struct task *t = (struct task *)kmalloc(sizeof(struct task));
    if (!t) return;

    t->func = func;
    t->arg = arg;
    t->priority = priority;
    t->running = 0; // Initialize running flag
    INIT_LIST_HEAD(&t->list);

    // 1. SAVE Current Interrupt State
    //    If called from Shell, SSTATUS_SIE is 1.
    //    If called from ISR, SSTATUS_SIE is 0.
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));

    // 2. Disable Interrupts for List Modification
    disable_interrupt();

    struct list_head *pos;
    struct task *curr;
    int inserted = 0;

    // Sorted Insert (Priority Queue)
    list_for_each(pos, &task_queue) {
        curr = list_entry(pos, struct task, list);
        if (curr->priority > t->priority) {
            list_add_tail(&t->list, pos);
            inserted = 1;
            break;
        }
    }

    if (!inserted) {
        list_add_tail(&t->list, &task_queue);
    }

    // 3. RESTORE Interrupt State
    //    Only re-enable if they were ON when we started.
    if (sstatus & SSTATUS_SIE) {
        enable_interrupt();
    }
}

void task_run() {
    while (1) {
        // 1. Lock to inspect the list safely
        disable_interrupt();

        if (list_empty(&task_queue)) {
            // Nothing to do. Return to Dispatcher.
            // (Interrupts remain DISABLED)
            return;
        }

        // 2. Peek at the highest priority task (Do not remove yet!)
        struct task *t = list_first_entry(&task_queue, struct task, list);

        // 3. Check if it's already running (Recursive prevention)
        if (t->running) {
            // The head is currently running (we were preempted).
            // Since the list is sorted, and we are the head,
            // there is nothing higher priority to run. Return.
            return;
        }

        // 4. Mark as running
        t->running = 1;

        // 5. ENABLE INTERRUPTS and RUN
        //    (Allows Preemption by higher priority tasks)
        enable_interrupt();

        if (t->func) {
            t->func(t->arg);
        }

        // 6. Disable Interrupts to remove from list
        disable_interrupt();
        
        // 7. Remove and Free
        list_del(&t->list);
        kfree(t);
        
        // 8. Re-enable to loop again (Check for next task)
        enable_interrupt();
    }
}