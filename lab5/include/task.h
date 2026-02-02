/* src/task.h */
#ifndef _TASK_H_
#define _TASK_H_

#include "list.h"

// Priorities (Lower value = Higher Priority)
#define PRIORITY_HIGH   0 
#define PRIORITY_NORMAL 1 

typedef void (*task_func_t)(void *arg);

struct task {
    struct list_head list;
    task_func_t func;
    void *arg;
    int priority;
    int running; // [NEW] Track execution state (Reference Logic)
};

void task_init();
void task_add(task_func_t func, void *arg, int priority);
void task_run();

#endif