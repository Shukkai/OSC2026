/* src/signal.c */
#include "signal.h"
#include "sched.h"
#include "sys.h"
#include "vm.h"
#include "mm.h"
#include "string.h"

static unsigned long signal_trampoline_va(void) {
    return (SIGNAL_STACK_TOP - 16) & ~15UL;
}

void release_signal_stack(struct task_struct *task) {
    int unmapped = 0;

    if (!task) return;

    if (task->signal_stack_va && task->mm.pgd) {
        unsigned long *pte = pagewalk(task->mm.pgd, task->signal_stack_va, 0);
        if (pte && (*pte & PAGE_PRESENT)) {
            *pte = 0;
            unmapped = 1;
        }
    }

    if (task->signal_stack_phys) {
        dec_page_ref(task->signal_stack_phys);
    }

    task->signal_stack_va = 0;
    task->signal_stack_phys = 0;
    task->in_signal_handler = 0;

    if (unmapped) {
        asm volatile("sfence.vma zero, zero" ::: "memory");
    }
}

void handle_signal(struct TrapFrame *tf) {
    if (!current || current->in_signal_handler || !current->pending_signals) return;

    for (int i = 0; i < MAX_SIG; i++) {
        if (!(current->pending_signals & (1U << i))) continue;

        current->pending_signals &= ~(1U << i);

        if (!current->signal_handler[i]) {
            do_exit(128 + i);
            return;
        }

        char *signal_stack = (char *)kmalloc(PAGE_SIZE);
        if (!signal_stack) {
            do_exit(-1);
            return;
        }
        memset(signal_stack, 0, PAGE_SIZE);

        unsigned long signal_stack_phys = virt_to_phys((unsigned long)signal_stack);
        unsigned long *pte = pagewalk(current->mm.pgd, SIGNAL_STACK_BASE, 1);
        if (!pte) {
            kfree(signal_stack);
            do_exit(-1);
            return;
        }

        release_signal_stack(current);

        *pte = (signal_stack_phys >> 12) << 10 |
               PAGE_PRESENT | PAGE_USER | PAGE_READ | PAGE_WRITE |
               PAGE_EXEC | PAGE_ACCESSED | PAGE_DIRTY;
        inc_page_ref(signal_stack_phys);

        uint32_t *trampoline = (uint32_t *)phys_to_virt(signal_stack_phys +
                                                        (signal_trampoline_va() - SIGNAL_STACK_BASE));
        trampoline[0] = 0x00b00893; // li a7, SYS_SIGRETURN
        trampoline[1] = 0x00000073; // ecall
        asm volatile("fence.i");
        asm volatile("sfence.vma zero, zero" ::: "memory");

        current->saved_tf = *tf;
        current->in_signal_handler = 1;
        current->signal_stack_va = SIGNAL_STACK_BASE;
        current->signal_stack_phys = signal_stack_phys;

        tf->sepc = (uint64_t)current->signal_handler[i];
        tf->sp = signal_trampoline_va();
        tf->ra = signal_trampoline_va();
        return;
    }
}
