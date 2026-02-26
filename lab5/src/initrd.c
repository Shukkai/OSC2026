#include "initrd.h"
#include "fdt.h"
#include "uart.h"
#include "string.h"
#include "utils.h" /* Include the new utils */
#include "mm.h"
#include "trap.h"
#include "sched.h"
extern unsigned long DTB_BASE;
extern void switch_to_user_mode(uint64_t pc, uint64_t sp);
extern void ret_from_exception(void);
/* Remove local parse_hex8 and align_up implementations */

void *get_initrd_base() {
    int chosen_offset = fdt_path_offset((void *)DTB_BASE, "/chosen");
    if (chosen_offset < 0) return NULL;

    int len;
    const void *prop = fdt_getprop((void *)DTB_BASE, chosen_offset, "linux,initrd-start", &len);
    if (!prop) return NULL;

    if (len == 8) {
        const uint32_t *val = (const uint32_t *)prop;
        /* USE bswap32 from utils.h instead of __builtin */
        uint64_t addr = ((uint64_t)bswap32(val[0]) << 32) | bswap32(val[1]);
        return (void *)addr;
    } 
    else if (len == 4) {
        /* USE bswap32 from utils.h */
        uint32_t val = bswap32(*(uint32_t *)prop);
        return (void *)(uintptr_t)val;
    }
    return NULL;
}

void initrd_list() {
    char *archive = (char *)get_initrd_base();
    if (!archive) {
        uart_puts("Error: Initrd not found in DTB.\n");
        return;
    }

    char *p = archive;
    
    while (1) {
        struct cpio_newc_header *header = (struct cpio_newc_header *)p;
        
        if (strncmp(header->c_magic, CPIO_NEWC_MAGIC, 6) != 0) {
            uart_puts("Error: Invalid CPIO Magic\n");
            return;
        }

        unsigned long namesize = parse_hex8(header->c_namesize);
        unsigned long filesize = parse_hex8(header->c_filesize);
        char *filename = p + sizeof(struct cpio_newc_header);
        
        if (strcmp(filename, "TRAILER!!!") == 0) break;

        uart_puts(filename);
        uart_puts("\n");

        uintptr_t next = (uintptr_t)p + sizeof(struct cpio_newc_header) + namesize;
        next = align_up(next, 4); /* Align to 4 bytes */
        next += filesize;
        next = align_up(next, 4); /* Align to 4 bytes */
        p = (char *)next;
    }
}

void initrd_cat(const char *target_filename) {
    char *archive = (char *)get_initrd_base();
    if (!archive) {
        uart_puts("Error: Initrd not found.\n");
        return;
    }

    char *p = archive;
    
    while (1) {
        struct cpio_newc_header *header = (struct cpio_newc_header *)p;
        
        if (strncmp(header->c_magic, CPIO_NEWC_MAGIC, 6) != 0) return;

        unsigned long namesize = parse_hex8(header->c_namesize);
        unsigned long filesize = parse_hex8(header->c_filesize);
        char *filename = p + sizeof(struct cpio_newc_header);

        if (strcmp(filename, "TRAILER!!!") == 0) break;

        uintptr_t content_start = (uintptr_t)p + sizeof(struct cpio_newc_header) + namesize;
        content_start = align_up(content_start, 4);
        
        if (strcmp(filename, target_filename) == 0) {
            char *content = (char *)content_start;
            for (unsigned long i = 0; i < filesize; i++) {
                uart_putc(content[i]);
            }
            uart_puts("\n");
            return;
        }

        uintptr_t next = content_start + filesize;
        next = align_up(next, 4);
        p = (char *)next;
    }
    uart_puts("File not found.\n");
}

void initrd_exec(const char *target_filename) {
    char *archive = (char *)get_initrd_base();
    if (!archive) {
        uart_puts("Error: Initrd not found.\n");
        return;
    }

    char *p = archive;
    
    while (1) {
        struct cpio_newc_header *header = (struct cpio_newc_header *)p;
        
        if (strncmp(header->c_magic, CPIO_NEWC_MAGIC, 6) != 0) {
            uart_puts("Error: Invalid CPIO Magic\n");
            return;
        }

        unsigned long namesize = parse_hex8(header->c_namesize);
        unsigned long filesize = parse_hex8(header->c_filesize);
        char *filename = p + sizeof(struct cpio_newc_header);

        if (strcmp(filename, "TRAILER!!!") == 0) break;

        uintptr_t content_start = align_up((uintptr_t)p + sizeof(struct cpio_newc_header) + namesize, 4);
        
        if (strcmp(filename, target_filename) == 0) {
            uart_puts("Loading '"); uart_puts(filename); uart_puts("'...\n");

            // 1. Create a REAL task for the user program
            struct task_struct *p = task_alloc();
            if (!p) {
                uart_puts("Error: Out of Memory for Task\n");
                return;
            }

            p->pid = pid_counter++;
            p->state = TASK_READY;
            p->priority = current->priority; // Inherit priority
            p->counter = 0;

            // 2. Allocate memory for the actual program code
            char *prog_buf = (char *)kmalloc(0x20000); 
            if (!prog_buf) return;

            // Copy file content
            char *src = (char *)content_start;
            for (unsigned long i = 0; i < filesize; i++) {
                prog_buf[i] = src[i];
            }
            asm volatile("fence.i");

            // 3. Setup TrapFrame for the new task
            p->tf = (struct TrapFrame *)(p->kernel_stack + PAGE_SIZE - sizeof(struct TrapFrame));
            
            // Zero out the TrapFrame for security/clean registers
            char *tf_bytes = (char *)p->tf;
            for(unsigned int i = 0; i < sizeof(struct TrapFrame); i++) {
                tf_bytes[i] = 0;
            }

            // Set entry point and user stack (stack grows DOWN from the top of the page)
            p->tf->sepc = (uint64_t)prog_buf;
            p->tf->sp = p->user_stack + PAGE_SIZE; 

            // Set sstatus: Clear SPP (drop to User Mode), Set SPIE (Enable Interrupts)
            p->tf->sstatus = (1 << 5); 

            // 4. Setup Kernel Context so switch_to() works
            p->thread.ra = (uint64_t)ret_from_exception;
            p->thread.sp = (uint64_t)p->tf;

            // 5. Add to scheduler safely
            disable_interrupt();
            list_add_tail(&p->list, &runqueue);
            enable_interrupt();

            // 6. Primitive Wait: Make the shell wait for the user program to finish!
            while (p->state != TASK_ZOMBIE) {
                schedule(); // Yield CPU to the user program
                enable_interrupt();
            }
            enable_interrupt();
            uart_puts("Process Exited. Returning to shell...\n");
            return; 
        }

        uintptr_t next = align_up(content_start + filesize, 4);
        p = (char *)next;
    }
    uart_puts("File not found: "); uart_puts(target_filename); uart_puts("\n");
}


void *initrd_find_file(const char *target_filename, unsigned long *out_filesize) {
    char *archive = (char *)get_initrd_base();
    if (!archive) return NULL;

    char *p = archive;
    while (1) {
        struct cpio_newc_header *header = (struct cpio_newc_header *)p;
        if (strncmp(header->c_magic, "070701", 6) != 0) return NULL;

        unsigned long namesize = parse_hex8(header->c_namesize);
        unsigned long filesize = parse_hex8(header->c_filesize);
        char *filename = p + sizeof(struct cpio_newc_header);

        if (strcmp(filename, "TRAILER!!!") == 0) break;

        uintptr_t content_start = align_up((uintptr_t)p + sizeof(struct cpio_newc_header) + namesize, 4);
        
        if (strcmp(filename, target_filename) == 0) {
            if (out_filesize) *out_filesize = filesize;
            return (void *)content_start;
        }

        uintptr_t next = align_up(content_start + filesize, 4);
        p = (char *)next;
    }
    return NULL;
}