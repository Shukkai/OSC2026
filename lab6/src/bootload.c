#include "bootload.h"
#include "uart.h"
#include "relocate.h"
#include "utils.h"

extern unsigned long boot_cpu_hartid;
extern unsigned long DTB_BASE;
extern char _start[];
extern char __stack_top[];

void cmd_load_kernel() {
    
    unsigned long safe_dtb = DTB_BASE;
    unsigned long safe_hartid = boot_cpu_hartid;

    // 1. Relocation Logic
    void *new_base = relocate_bootloader(safe_dtb);
    uint64_t dest_addr = (uint64_t)new_base;
    uint64_t current_pc = get_pc();

    // Check if we need to jump to the relocated code
    if (current_pc < dest_addr || current_pc > (dest_addr + 0x100000)) {
        uart_puts("[Reloc] Relocating to high memory...\n");

        uint64_t offset = dest_addr - (uint64_t)_start;
        
        // Calculate the function entry in the new location
        void (*relocated_func)() = (void *)((uint64_t)cmd_load_kernel + offset);
        
        // Calculate the new Stack Pointer in the new location
        // New SP = Old Stack Top + Offset
        uint64_t new_sp = (uint64_t)__stack_top + offset;

        uart_puts("[Reloc] Jumping to new location with new stack...\n");

        // Use Assembly to Switch SP and Jump
        // We reset SP to the top of the stack frame and jump to the start of the function.
        // This effectively "restarts" the function cleanly in high memory.
        asm volatile(
            "mv sp, %0 \n"       // Update Stack Pointer
            "mv s0, sp \n"       // Update Frame Pointer (optional but safe)
            "jr %1     \n"       // Jump to relocated function
            : 
            : "r" (new_sp), "r" (relocated_func)
            : "memory"
        );
        
        // Code below is never reached in the old location
        return; 
    }

    // --- Safe Memory Execution ---
    // At this point, we are running in High RAM, and SP is in High RAM.
    
    uint64_t current_sp;                                     
    asm volatile("mv %0, sp" : "=r"(current_sp));              

    uart_puts("[Reloc] Execution transferred.\n");
    uart_puts("[Reloc] New Stack Ptr:    "); uart_hex(current_sp); uart_puts("\n"); 
    uart_puts("\n--- Kernel Loader ---\n");
    
    char *kernel_dest;
    
    // Print Platform and Target Address nicely
    #if defined(__QEMU__)
        kernel_dest = (char *)(0x80200000); 
        uart_puts("Platform:         QEMU Virt\n");
    #else
        kernel_dest = (char *)(0x00200000); // Hardware Address
        uart_puts("Platform:         OrangePi RV2\n");
    #endif

    uart_puts("Load Addr:        "); uart_hex((unsigned long)kernel_dest); uart_puts("\n");

    int kn_ptr = 0;
    int idx = 0;
    char sz[50] = {};
    char c;
    
    // Handshake signal (Must contain "start loading" for Python script)
    uart_puts("Status:           Waiting for data (start loading)\n"); 
    
    while(1) {
        c = uart_getc_raw();
        if(c == '\n') {
            sz[idx] = '\0';
            break;
        }    
        sz[idx++] = c;
    }

    int size = atoi(sz); 
    uart_puts("Image Size:       "); uart_puts(sz); uart_puts(" bytes\n");

    while (size--) {
        kernel_dest[kn_ptr++] = uart_getc_raw();
    }

    uart_puts("Status:           Transfer Complete\n");
    // Flush instruction cache so the CPU sees the new code we just wrote!
    asm volatile("fence.i");
    int r = 10000;
    while(r--) { asm volatile("nop"); }

    uart_puts("Entry Point:      "); uart_hex((unsigned long)kernel_dest); uart_puts("\n");
    uart_puts("---------------------\n");

    
    print_dtb_info(safe_dtb); // Use safe_dtb

    void (*run)(unsigned long, unsigned long) = (void *)kernel_dest;
    
    // Jump using the cached values
    run(safe_hartid, safe_dtb);
}