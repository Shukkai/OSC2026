/* include/signal.h */
#pragma once
#include "trap.h"

struct task_struct;

#define SIGNAL_STACK_BASE 0x3fffffb000UL
#define SIGNAL_STACK_TOP  (SIGNAL_STACK_BASE + 0x1000UL)

// Standard RISC-V Signal delivery 
void handle_signal(struct TrapFrame *tf);
void release_signal_stack(struct task_struct *task);
