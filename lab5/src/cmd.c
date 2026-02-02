#include "cmd.h"
#include "uart.h"
#include "string.h"
#include "sbi.h"
#include "fdt.h" 
#include "initrd.h"
#include "bootload.h"
#include "utils.h"
#include "mm.h"
#include "timer.h"
#include "printk.h"
#include "task.h"
#include "trap.h"
#include "sched.h"
/* Access global variables defined in kernel.c */
extern unsigned long boot_cpu_hartid;
extern unsigned long DTB_BASE;

/* ========================================================================= */
/* DEMO FUNCTIONS                                                            */
/* ========================================================================= */
void foo() {
    for (int i = 0; i < 5; i++) {
        printk("Thread id: %d %d\n", get_current()->pid, i);
        for (int i = 0; i < 100000000; i++);
        schedule();
    }
    kthread_exit();
}

void demo_sched() {
    uart_puts("\n=== SCHEDULER TEST ===\n");
    uart_puts("Creating 3 threads (PID 1, 2, 3)...\n");

    thread_create(foo, NULL);
    thread_create(foo, NULL);
    thread_create(foo, NULL);

    idle();
    uart_puts("All tasks done.\n");
}

void task_heavy_work(void *arg) {
    char *name = (char *)arg;
    
    uart_puts("\n[Start] "); uart_puts(name); uart_puts("\n");
    
    // Print dots slowly to visualize time passing
    for (int i = 0; i < 10; i++) {
        uart_puts(name); 
        uart_puts(".");
        
        // Busy Wait (approx 200ms) - DOES NOT YIELD CPU!
        // We use this to force the CPU to stay in this function.
        // If interrupts were disabled, the system would freeze here.
        // Since interrupts are ENABLED (task_run), UART IRQs should still work!
        for(volatile int j=0; j<50000000; j++); 
    }
    
    uart_puts("\n[End] "); uart_puts(name); uart_puts("\n");
}
void demo_task() {
    uart_puts("\n=== TASK QUEUE & PREEMPTION TEST ===\n");
    
    // CRITICAL: Disable interrupts so we   can queue BOTH tasks 
    // before the Timer (Scheduler) picks one.
    disable_interrupt();

    uart_puts("1. Queuing 'Task Low' (Priority Normal)...\n");
    task_add(task_heavy_work, "Low", PRIORITY_NORMAL);

    uart_puts("2. Queuing 'Task High' (Priority High)...\n");
    task_add(task_heavy_work, "High", PRIORITY_HIGH);

    uart_puts("---------------------------------------------------\n");
    uart_puts("Tasks queued atomically. Re-enabling interrupts...\n");
    uart_puts("EXPECTATION: 'High' runs first, then 'Low'.\n");
    uart_puts("---------------------------------------------------\n");

    // Enable Interrupts -> Timer Fires -> task_run() sees both -> Picks High
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
    uart_puts("\n=== Buddy System Test ===\n");
    buddy_verbose = 1; // Ensure verbose is ON

    // --- CHANGE 1: Request Order 9 (512 Pages) ---
    // This forces the system to split a Max-Order (10) block.
    // If we asked for Order 0 or 2, we might just find a loose fragment.
    uart_puts("Allocating 512 Pages (Order 9)...\n");
    struct page *p_large = alloc_pages(9); 
    
    if (p_large) {
        uart_puts("-> Allocated Order 9 at PFN: "); 
        uart_hex(page_to_pfn(p_large)); 
        uart_puts("\n");
        
        // --- CHANGE 2: Free it immediately ---
        // Since we just split an Order 10 block, the other half (buddy)
        // is definitely free. This GUARANTEES a merge log.
        uart_puts("Freeing Order 9... (Expect MERGE logs)\n");
        free_pages(p_large, 9);
    } else {
        uart_puts("-> [Fail] Out of memory for Order 9.\n");
    }

    buddy_verbose = 0; // Turn off verbose
    uart_puts("=== Buddy Test Complete ===\n");
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
    uart_puts("  exec        - Execute user_prog\n");
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
        uint64_t uart_addr = fdt_get_uart_base((void *)DTB_BASE);
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
        else {
            uart_puts("Unknown demo option: "); uart_puts(arg); uart_puts("\n");
            demo_help();
        }
    }
    else if (!strcmp(buf, "exec")) {
        initrd_exec("user_prog");
    }
    else {
        uart_puts("Unknown command: "); uart_puts(buf); uart_puts("\n");
    }
}