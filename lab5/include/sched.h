/* src/sched.h */
#ifndef _SCHED_H_
#define _SCHED_H_

#include "list.h"
#include "trap.h"


#define MAX_SIG 32
#define SIGTERM 15  `

struct thread_struct {
    uint64_t ra;
    uint64_t sp;
    uint64_t s[12];
};

enum task_state {
    TASK_UNUSED,
    TASK_RUNNING,
    TASK_READY,
    TASK_ZOMBIE,
    TASK_EXITED
};

struct task_struct {
    struct thread_struct thread; 

    uint64_t kernel_stack;
    uint64_t user_stack;         
    struct TrapFrame *tf;        

    long pid;
    enum task_state state;
    int counter;                 
    int priority;
    int preempt_count;           
    int exit_code;
    struct list_head list;       

    // Signal Management
    void (*signal_handler[MAX_SIG])(void); // Array of handler functions
    unsigned int pending_signals;          // Bitmask of waiting signals
    struct TrapFrame saved_tf;             // Saved state before handling signal
    int in_signal_handler;                 // Prevents nested signal loops
};
// Global Scheduler Variables
extern int pid_counter;
extern struct list_head runqueue; 
extern struct task_struct *current;


struct task_struct *get_current();
int num_runnable_tasks(void);

void sched_init(void);
void schedule(void);
void switch_to(struct thread_struct *prev, struct thread_struct *next);

struct task_struct *thread_create(void (*start_routine)(void *), void *arg);
void kthread_exit(void); 

void kill_zombies();
void idle();
// [Helper] Allocates a new task and its stacks. Returns NULL on failure.
struct task_struct *task_alloc();
struct task_struct *find_task_by_pid(int pid);
#endif // _SCHED_H_