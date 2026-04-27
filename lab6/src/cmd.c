#include "cmd.h"
#include "uart.h"
#include "string.h"
#include "sbi.h"
#include "fdt.h" 
#include "initrd.h"
#include "bootload.h"
#include "utils.h"
#include "mm.h"
#include "vm.h"
#include "sys.h"
#include "timer.h"
#include "printk.h"
#include "task.h"
#include "trap.h"
#include "sched.h"
#include "forktest.h"
/* Access global variables defined in kernel.c */
extern unsigned long boot_cpu_hartid;
extern unsigned long DTB_BASE;

/* ========================================================================= */
/* DEMO FUNCTIONS                                                            */
/* ========================================================================= */
void foo(void *arg) {
    (void)arg; // Now the compiler knows what 'arg' is, and we mark it unused.
    
    for (int i = 0; i < 5; i++) {
        printk("Thread id: %d %d\n", get_current()->pid, i);
        for (int j = 0; j < 100000000; j++); // Changed inner 'i' to 'j' to avoid shadowing warnings
        schedule();
    }
    kthread_exit();
}

void demo_sched() {
    uart_puts("\n=== SCHEDULER TEST ===\n");
    uart_puts("Creating 5 threads (PID 1, 2, 3)...\n");
    for(int i = 0; i < 5; ++i) {
        thread_create(foo, NULL);
    }
    idle();
    uart_puts("All tasks done.\n");
}

/* ========================================================================= */
/* DEMO FUNCTIONS                                                            */
/* ========================================================================= */

/* ========================================================================= */
/* DEMO FUNCTIONS                                                            */
/* ========================================================================= */

int priority_set[4];

void p1_callback(void *arg) {
    (void)arg;
    uart_puts("P1 start\n");
    uart_puts("P1 end\n");
}

void p3_callback(void *arg) {
    (void)arg;
    uart_puts("P3 start\n");
    task_add(p1_callback, NULL, priority_set[0]);
    
    // 使用 timer 觸發中斷來達成 preemption
    timer_add(NULL, NULL, 0); 
    
    uart_puts("P3 end\n");
}

void p2_callback(void *arg) {
    (void)arg;
    uart_puts("P2 start\n");
    task_add(p3_callback, NULL, priority_set[2]);
    
    timer_add(NULL, NULL, 0); 
    
    uart_puts("P2 end\n");
}

void p4_callback(void *arg) {
    (void)arg;
    uart_puts("P4 start\n");
    task_add(p2_callback, NULL, priority_set[1]);
    
    timer_add(NULL, NULL, 0); 
    
    uart_puts("P4 end\n");
}

void demo_task() {
    uart_puts("\n=== TASK QUEUE & PREEMPTION TEST ===\n");
    
    int from_small_to_big = 1; // set to 0 if the task with a smaller number has a higher priority
    if (from_small_to_big) {
        priority_set[0] = 10;
        priority_set[1] = 20;
        priority_set[2] = 30;
        priority_set[3] = 40;
    } else {
        priority_set[0] = 40;
        priority_set[1] = 30;
        priority_set[2] = 20;
        priority_set[3] = 10;
    }

    // Disable interrupts to queue the first task atomically
    disable_interrupt();
    task_add(p4_callback, NULL, priority_set[3]);
    enable_interrupt();
}

void demo_help() {
    uart_puts("Usage: demo [option]\n");
    uart_puts("Options:\n");
    uart_puts("  buddy   - Test Buddy System (Split/Merge logic)\n");
    uart_puts("  slab    - Test SLAB Allocator (Reuse/Packing logic)\n");
    uart_puts("  timer   - Test Timer Interrupts (Timeout & Sleep)\n");
    uart_puts("  task    - Test Task Queue Priority & Preemption\n");
    uart_puts("  sched   - Test Kernel Threads\n");
    uart_puts("  fork    - Test Process Forking\n");
}

/* Callback for Strong Test */
static void timer_test_callback(void *arg) {
    char *name = (char *)arg;
    unsigned long now = get_uptime();
    printk("[StrongTest] %s callback fired at %ds\n", name, now);
}

void demo_timer() {
    uart_puts("\n=== TIMER TEST SUITE ===\n");
    unsigned long start = get_uptime();
    printk("Start Uptime: %ds\n", start);

    // --- PART 1: Basic Async & Blocking Sleep ---
    uart_puts("\n[Part 1] Basic Async & Sleep Test\n");
    
    // Set a timeout that should fire 3 seconds from NOW (T+0)
    uart_puts("1. Setting Async Timeout for 3 seconds...\n");
    set_timeout("[Async] -> Part 1 Timeout (3s) fired!\n", 3);

    // Test Blocking Sleep (This moves time forward by 1s)
    uart_puts("2. Testing Blocking Sleep (1s)... ");
    sleep(1000); 
    uart_puts("Done.\n");

    unsigned long now = get_uptime();
    if (now > start) {
        uart_puts("-> [PASS] Time advanced (Blocking sleep works).\n");
    } else {
        uart_puts("-> [FAIL] Time did not advance.\n");
    }

    // --- PART 2: Strong Concurrent Test ---
    // Note: Time is now (Start + 1s).
    uart_puts("\n[Part 2] Strong Concurrent Timer Test\n");
    uart_puts("Adding 5 timers in random order (3s, 5s, 1s, 2s, 4s)...\n");

    // These delays are added relative to CURRENT time (Start + 1s).
    // So 'Timer A (1s)' will fire at (Start + 1s + 1s) = Start + 2s.
    
    timer_add(timer_test_callback, "Timer C (3s)", 3);
    timer_add(timer_test_callback, "Timer E (10s)", 10);
    timer_add(timer_test_callback, "Timer A (1s)", 1);
    timer_add(timer_test_callback, "Timer B (2s)", 2);
    timer_add(timer_test_callback, "Timer D (4s)", 4);

    uart_puts("All timers set. Waiting for interrupts...\n");
    uart_puts("---------------------------------------------------\n");
    uart_puts("Expected Timeline (approximate):\n");
    uart_puts(" +1s from start: (We are here)\n");
    uart_puts(" +2s from start: [StrongTest] Timer A (1s)\n");
    uart_puts(" +3s from start: [Async] Part 1 Timeout & [StrongTest] Timer B (2s)\n");
    uart_puts(" +4s from start: [StrongTest] Timer C (3s)\n");
    uart_puts(" +5s from start: [StrongTest] Timer D (4s)\n");
    uart_puts(" +6s from start: [StrongTest] Timer E (10s)\n");
    uart_puts("---------------------------------------------------\n");
}
void demo_buddy() {
    buddy_verbose = 1;
    uart_puts("Testing memory allocation...\n");
    char *ptr1 = (char *)kmalloc(4000);
    char *ptr2 = (char *)kmalloc(8000);
    char *ptr3 = (char *)kmalloc(4000);
    char *ptr4 = (char *)kmalloc(4000);

    kfree(ptr1);
    kfree(ptr2);
    kfree(ptr3);
    kfree(ptr4);

    /* Test kmalloc */
    uart_puts("Testing dynamic allocator...\n");
    char *kmem_ptr1 = (char *)kmalloc(16);
    char *kmem_ptr2 = (char *)kmalloc(32);
    char *kmem_ptr3 = (char *)kmalloc(64);
    char *kmem_ptr4 = (char *)kmalloc(128);

    kfree(kmem_ptr1);
    kfree(kmem_ptr2);
    kfree(kmem_ptr3);
    kfree(kmem_ptr4);

    char *kmem_ptr5 = (char *)kmalloc(16);
    char *kmem_ptr6 = (char *)kmalloc(32);

    kfree(kmem_ptr5);
    kfree(kmem_ptr6);

    // Test allocate new page if the cache is not enough
    void *kmem_ptr[102];
    for (int i=0; i<100; i++) {
        kmem_ptr[i] = (char *)kmalloc(128);
    }
    for (int i=0; i<100; i++) {
        kfree(kmem_ptr[i]);
    }

    // Test exceeding the maximum size
    char *kmem_ptr7 = (char *)kmalloc(MAX_ALLOC_SIZE + 1);
    if (kmem_ptr7 == NULL) {
        uart_puts("Allocation failed as expected for size > MAX_ALLOC_SIZE\n");
    }
    else {
        uart_puts("Unexpected allocation success for size > MAX_ALLOC_SIZE\n");
        kfree(kmem_ptr7);
    }
    buddy_verbose = 0; 
}

void demo_slab() {
    uart_puts("\n=== SLAB Allocator Test ===\n");
    
    // 1. Packing Test (Allocating multiple small objects)
    uart_puts("[Test 1] Packing Test (16 bytes)\n");
    void *a = kmalloc(16);
    void *b = kmalloc(16);
    uart_puts("Obj A: "); uart_hex((uint64_t)a); uart_puts("\n");
    uart_puts("Obj B: "); uart_hex((uint64_t)b); uart_puts("\n");

    // Check if they are contiguous (0x10 = 16 bytes)
    if ((uint64_t)b - (uint64_t)a == 16) {
        uart_puts("-> [PASS] Objects are contiguous.\n");
    } else {
        uart_puts("-> [WARN] Objects are far apart (Check cache/packing logic).\n");
    }

    // 2. Reuse Test (Free and Re-allocate)
    uart_puts("\n[Test 2] Reuse Test\n");
    uart_puts("Freeing A...\n");
    kfree(a);
    
    void *c = kmalloc(16);
    uart_puts("Obj C: "); uart_hex((uint64_t)c); uart_puts(" (Allocated after freeing A)\n");

    if (c == a) {
        uart_puts("-> [PASS] Address reused successfully.\n");
    } else {
        uart_puts("-> [WARN] Address NOT reused (Leaky?).\n");
    }

    // 3. Cleanup
    kfree(b);
    kfree(c);

    // 4. Large Alloc Pass-through
    uart_puts("\n[Test 3] Large Alloc (4096 bytes)\n");
    void *d = kmalloc(4096);
    uart_puts("Obj D: "); uart_hex((uint64_t)d); uart_puts("\n");
    kfree(d);

    uart_puts("=== SLAB Test Complete ===\n");
}


/* ========================================================================= */
/* CORE COMMANDS                                                             */
/* ========================================================================= */

void cmd_help()
{
    uart_puts("Available commands:\n");
    uart_puts("  help        - Show this message\n");
    uart_puts("  hello       - Print Hello World\n");
    uart_puts("  reboot      - Reboot system (SBI Shutdown)\n");
    uart_puts("  info        - Show System Info\n");
    uart_puts("  clear       - Clear Terminal\n");
    uart_puts("  ls          - List initial ramdisk (initrd) files\n");
    uart_puts("  cat <file>  - Output initrd file content\n");
    uart_puts("  load        - Load kernel over UART\n");
    uart_puts("  exec [file] - Execute user program (default: osctest.bin, opts: osctest.bin, user_prog)\n");
    uart_puts("  demo [opt]  - Run demos (buddy, slab, timer, task, sched)\n");
}

void cmd_clear(){
    uart_puts("\033[2J\033[1;1H");
}

void cmd_init()
{
    uart_puts("\n");
    uart_puts(" ==================================\n");
    uart_puts("      Welcome to  NYCU OSC         \n");
    uart_puts("      Type 'help' for commands     \n");
    uart_puts(" ==================================\n");
}

void exec_command(char *buf)
{
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == ' ')) {
        buf[--len] = '\0';
    }
    if (len == 0) return;

    // --- Command Routing ---

    if (!strcmp(buf, "help")) {
        cmd_help();
    } 
    else if (!strcmp(buf, "hello")) {
        uart_puts("Hello World!\n");
    } 
    else if (!strcmp(buf, "reboot")) {
        uart_puts("Rebooting system...\n");
        sbi_system_reset(SBI_RESET_TYPE_COLD_REBOOT, SBI_RESET_REASON_NONE);
        uart_puts("Reset failed.\n");
    } 
    else if (!strcmp(buf, "info")) {
        struct sbiret ret;

        uart_puts("\n--- System Info ---\n");
        uart_puts("Hart ID:          "); uart_hex(boot_cpu_hartid); uart_puts("\n");
        
        // 1. Print DTB Info using the new helper
        print_dtb_info(DTB_BASE);

        // 2. SBI Version Info
        ret = sbi_get_spec_version();
        uart_puts("SBI Spec Version: "); uart_hex(ret.value); uart_puts("\n");
        ret = sbi_get_impl_id();
        uart_puts("SBI Impl ID:      "); uart_hex(ret.value); uart_puts("\n");
        ret = sbi_get_impl_version();
        uart_puts("SBI Impl Version: "); uart_hex(ret.value); uart_puts("\n");

        // 3. Get UART info
        uint64_t uart_addr = fdt_get_uart_base((const void *)phys_to_virt(DTB_BASE));
        if (uart_addr) {
            uart_puts("UART Base:        "); uart_hex(uart_addr); uart_puts("\n");
        } else {
            uart_puts("UART Base:        Not Found\n");
        }
        
        uart_puts("-------------------\n");
    }
    else if(!strcmp(buf, "clear")){
        cmd_clear();
    }
    else if (!strcmp(buf, "ls")) {
        initrd_list();
    }
    else if (strncmp(buf, "cat ", 4) == 0) {
        initrd_cat(buf + 4);
    }
    else if (!strcmp(buf, "load")) {
        cmd_load_kernel();
    }
    // --- New Modular Demo Logic ---
    else if (strncmp(buf, "demo", 4) == 0) {
        // Check for arguments (e.g., "demo buddy")
        char *arg = buf + 4;
        
        // Skip spaces
        while (*arg == ' ') arg++;

        if (*arg == '\0') {
            // No argument provided -> Show Help
            demo_help();
        } 
        else if (!strcmp(arg, "buddy")) {
            demo_buddy();
        } 
        else if (!strcmp(arg, "slab")) {
            demo_slab();
        } 
        else if (!strcmp(arg, "timer")) {
            demo_timer();
        }
        else if (!strcmp(arg, "task")) {
            demo_task();
        }
        else if (!strcmp(arg, "sched")) {
            demo_sched();
        }
        else if (!strcmp(arg, "fork")) {
            // test_fork();
            uart_puts("Disabled, test with osctest.bin instead.\n");
        }
        else {
            uart_puts("Unknown demo option: "); uart_puts(arg); uart_puts("\n");
            demo_help();
        }
    }
    else if (strncmp(buf, "exec", 4) == 0) {
        char *arg = buf + 4;
        while (*arg == ' ') arg++;
        if (*arg == '\0'){
            arg = "osctest.bin";
            disable_interrupt();
            uart_puts("No file specified. Defaulting to '");
            uart_puts(arg);
            uart_puts("'\n");
            enable_interrupt();
        }
        
        initrd_exec(arg);  // creates user task, waits for it to finish
        
    }
    else {
        uart_puts("Unknown command: "); uart_puts(buf); uart_puts("\n");
    }
}