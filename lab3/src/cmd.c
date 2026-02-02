#include "cmd.h"
#include "uart.h"
#include "string.h"
#include "sbi.h"
#include "fdt.h" 
#include "initrd.h"
#include "bootload.h"
#include "utils.h"
#include "mm.h"

/* Access global variables defined in kernel.c */
extern unsigned long boot_cpu_hartid;
extern unsigned long DTB_BASE;

/* ========================================================================= */
/* DEMO FUNCTIONS                                                            */
/* ========================================================================= */

void demo_help() {
    uart_puts("Usage: demo [option]\n");
    uart_puts("Options:\n");
    uart_puts("  buddy   - Test Buddy System (Split/Merge logic)\n");
    uart_puts("  slab    - Test SLAB Allocator (Reuse/Packing logic)\n");
}

void demo_buddy() {
    uart_puts("\n=== Buddy System Test ===\n");
    buddy_verbose = 1; // Ensure verbose is ON

    // 1. Allocate a LARGE block (Order 2 = 4 Pages)
    // This forces the allocator to find a free Order 2 block (or split an Order 3)
    uart_puts("Allocating 4 Pages (Order 2)...\n");
    struct page *p_large = alloc_pages(2);
    
    if (p_large) {
        uart_puts("-> Allocated Order 2 at PFN: "); 
        uart_hex(page_to_pfn(p_large)); 
        uart_puts("\n");
        
        // 2. Free it immediately
        // This guarantees a merge because we just split it/allocated it cleanly
        uart_puts("Freeing Order 2... (Expect MERGE logs)\n");
        free_pages(p_large, 2);
    } else {
        uart_puts("-> [Fail] Out of memory for Order 2.\n");
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
    uart_puts("  demo [opt]  - Run demos (buddy, slab)\n");
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
        else {
            uart_puts("Unknown demo option: "); uart_puts(arg); uart_puts("\n");
            demo_help();
        }
    }
    else {
        uart_puts("Unknown command: "); uart_puts(buf); uart_puts("\n");
    }
}