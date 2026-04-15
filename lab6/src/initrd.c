#include "initrd.h"
#include "fdt.h"
#include "uart.h"
#include "string.h"
#include "utils.h" /* Include the new utils */
#include "mm.h"
#include "trap.h"
#include "sched.h"
#include "vm.h"
#include "printk.h"
extern unsigned long DTB_BASE;
// extern void switch_to_user_mode(uint64_t pc, uint64_t sp);
extern void ret_from_exception(void);
extern unsigned long pg_dir[]; // Defined in vm.c

void *get_initrd_base() {
    void *dtb_va = (void *)phys_to_virt(DTB_BASE);
    int chosen_offset = fdt_path_offset(dtb_va, "/chosen");
    if (chosen_offset < 0) return NULL;

    int len;
    const void *prop = fdt_getprop(dtb_va, chosen_offset, "linux,initrd-start", &len);
    if (!prop) return NULL;

    if (len == 8) {
        const uint32_t *val = (const uint32_t *)prop;
        uint64_t addr = ((uint64_t)bswap32(val[0]) << 32) | bswap32(val[1]);
        return (void *)phys_to_virt(addr);
    } 
    else if (len == 4) {
        uint32_t val = bswap32(*(uint32_t *)prop);
        return (void *)phys_to_virt((uintptr_t)val);
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

static struct task_struct *create_user_task(uintptr_t content_start,
                                             unsigned long filesize) {
    struct task_struct *task = task_alloc();
    if (!task) return NULL;

    task->pid = pid_counter++;
    task->state = TASK_READY;
    task->priority = current->priority;
    task->counter = 0;

    // --- 1. Page table ---
    unsigned long *user_pgd = (unsigned long *)kmalloc(PAGE_SIZE);
    if (!user_pgd) { kfree((void*)task->kernel_stack); kfree((void*)task->user_stack); kfree(task); return NULL; }

    for (int i = 0; i < 512; i++) user_pgd[i] = 0;

    for (int i = 256; i < 512; i++) user_pgd[i] = pg_dir[i];

    task->mm.pgd = user_pgd;

    // --- 2. Map text at VA 0x0 ---
    unsigned long text_pages = (filesize + PAGE_SIZE - 1) / PAGE_SIZE;
    for (unsigned long i = 0; i < text_pages; i++) {
        char *prog_buf = (char *)kmalloc(PAGE_SIZE);
        if (!prog_buf) { kfree(user_pgd); kfree((void*)task->kernel_stack); kfree((void*)task->user_stack); kfree(task); return NULL; }
        
        unsigned long offset = i * PAGE_SIZE;
        unsigned long copy_len = filesize - offset;
        if (copy_len > PAGE_SIZE) copy_len = PAGE_SIZE;
        
        char *src = (char *)(content_start + offset);
        for (unsigned long j = 0; j < copy_len; j++)
            prog_buf[j] = src[j];
        for (unsigned long j = copy_len; j < PAGE_SIZE; j++)
            prog_buf[j] = 0;
        
        map_pages(user_pgd, 0x0 + i * PAGE_SIZE,
                  virt_to_phys((unsigned long)prog_buf), PAGE_SIZE,
                  PAGE_PRESENT | PAGE_READ | PAGE_EXEC | PAGE_USER | PAGE_ACCESSED | PAGE_DIRTY);
    }


    // --- 3. Map stack: 4 pages at 0x3fffffc000 ---
    // for (int i = 0; i < 4; i++) {
    //     char *sp = (char *)kmalloc(PAGE_SIZE);
    //     if (!sp) { kfree(user_pgd); kfree((void*)task->kernel_stack); kfree((void*)task->user_stack); kfree(task); return NULL; }
    //     map_pages(user_pgd, 0x3fffffc000UL + i * PAGE_SIZE,
    //               virt_to_phys((unsigned long)sp), PAGE_SIZE,
    //               PAGE_PRESENT | PAGE_READ | PAGE_WRITE | PAGE_USER | PAGE_ACCESSED | PAGE_DIRTY);
    // }
    for (int i = 0; i < 4; i++) {
        char *sp = (char *)kmalloc(PAGE_SIZE);
        if (!sp) { kfree(user_pgd); kfree((void*)task->kernel_stack); kfree((void*)task->user_stack); kfree(task); return NULL; }
        map_pages(user_pgd, 0x3fffffc000UL + i * PAGE_SIZE,
                  virt_to_phys((unsigned long)sp), PAGE_SIZE,
                  PAGE_PRESENT | PAGE_READ | PAGE_WRITE | PAGE_EXEC | PAGE_USER | PAGE_ACCESSED | PAGE_DIRTY);
    }



    task->tf = (struct TrapFrame *)(task->kernel_stack + PAGE_SIZE - sizeof(struct TrapFrame));
    memset(task->tf, 0, sizeof(struct TrapFrame));
    task->tf->sepc    = 0x0;
    task->tf->sp      = 0x4000000000UL;
    task->tf->sstatus = (1 << 5);

    // --- 5. Kernel context ---
    task->thread.ra = (uint64_t)ret_from_exception;
    task->thread.sp = (uint64_t)task->tf;
    return task;
}

void initrd_exec(const char *target_filename) {
    char *archive = (char *)get_initrd_base();
    if (!archive) { uart_puts("Error: Initrd not found.\n"); return; }

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

        uintptr_t content_start = align_up(
            (uintptr_t)p + sizeof(struct cpio_newc_header) + namesize, 4);

        if (strcmp(filename, target_filename) == 0) {
            uart_puts("Loading '"); uart_puts(filename); uart_puts("'...\n");

            struct task_struct *task = create_user_task(content_start, filesize);
            if (!task) { uart_puts("Error: OOM\n"); return; }

            disable_interrupt();
            list_add_tail(&task->list, &runqueue);
            enable_interrupt();

            // while (task->state != TASK_ZOMBIE)
            //     schedule();
            while (task->state != TASK_ZOMBIE) {
                kill_zombies();
                schedule();
            }
            // Wait for all child processes to finish too
            // while (num_runnable_tasks() > 1) {
            //     kill_zombies();
            //     schedule();
            // }
            kill_zombies();
            enable_interrupt();
            uart_puts("Process Exited. Returning to shell...\n");
            return;
        }

        uintptr_t next = align_up(content_start + filesize, 4);
        p = (char *)next;
    }
    uart_puts("File not found: "); uart_puts(target_filename); uart_puts("\n");
}

// void initrd_exec(const char *target_filename) {
//     char *archive = (char *)get_initrd_base();
//     if (!archive) {
//         uart_puts("Error: Initrd not found.\n");
//         return;
//     }

//     char *p = archive;
    
//     while (1) {
//         struct cpio_newc_header *header = (struct cpio_newc_header *)p;
        
//         if (strncmp(header->c_magic, CPIO_NEWC_MAGIC, 6) != 0) {
//             uart_puts("Error: Invalid CPIO Magic\n");
//             return;
//         }

//         unsigned long namesize = parse_hex8(header->c_namesize);
//         unsigned long filesize = parse_hex8(header->c_filesize);
//         char *filename = p + sizeof(struct cpio_newc_header);

//         if (strcmp(filename, "TRAILER!!!") == 0) break;

//         uintptr_t content_start = align_up((uintptr_t)p + sizeof(struct cpio_newc_header) + namesize, 4);
        
//         if (strcmp(filename, target_filename) == 0) {
//             // 1. Create a REAL task for the user program
//             struct task_struct *task = task_alloc();
//             uart_puts("[ie] ks="); uart_hex(task->kernel_stack); uart_puts("\n");
// uart_puts("[ie] tf="); uart_hex(task->kernel_stack + PAGE_SIZE - sizeof(struct TrapFrame)); uart_puts("\n");
//             if (!task) {
//                 uart_puts("Error: Out of Memory for Task\n");
//                 return;
//             }
//             uart_puts("[dbg] kernel_stack="); uart_hex(task->kernel_stack); uart_puts("\n");

//             task->pid = pid_counter++;
//             task->state = TASK_READY;
//             task->priority = current->priority; // Inherit priority
//             task->counter = 0;

//              // --- 1. Allocate user page table ---
//             unsigned long *user_pgd = (unsigned long *)kmalloc(PAGE_SIZE);
//             for (int i = 0; i < 512; i++) user_pgd[i] = 0;

//             // Copy kernel mappings (upper half)
//             for (int i = 256; i < 512; i++)
//                 user_pgd[i] = pg_dir[i];

//             task->mm.pgd = user_pgd;

//             // --- 2. Map user text at VA 0x0 ---
//             char *prog_buf = (char *)kmalloc(PAGE_SIZE);
//             char *src = (char *)content_start;
//             for (unsigned long i = 0; i < filesize; i++)
//                 prog_buf[i] = src[i];

//             unsigned long prog_pa = virt_to_phys((unsigned long)prog_buf);
//             unsigned long text_flags = PAGE_PRESENT | PAGE_READ | PAGE_EXEC |
//                                     PAGE_USER | PAGE_ACCESSED | PAGE_DIRTY;
//             map_pages(user_pgd, 0x0, prog_pa, PAGE_SIZE, text_flags);

//             // --- 3. Map user stack: 4 pages at 0x3fffffc000 ---
//             unsigned long stack_flags = PAGE_PRESENT | PAGE_READ | PAGE_WRITE |
//                                         PAGE_USER | PAGE_ACCESSED | PAGE_DIRTY;
//             for (int i = 0; i < 4; i++) {
//                 char *sp = (char *)kmalloc(PAGE_SIZE);
//                 unsigned long sp_pa = virt_to_phys((unsigned long)sp);
//                 map_pages(user_pgd, 0x3fffffc000UL + i * PAGE_SIZE,
//                         sp_pa, PAGE_SIZE, stack_flags);
//             }

//             // --- 4. Setup TrapFrame ---
//             task->tf = (struct TrapFrame *)(task->kernel_stack + PAGE_SIZE - sizeof(struct TrapFrame));
//             char *tf_bytes = (char *)task->tf;
//             for (unsigned int i = 0; i < sizeof(struct TrapFrame); i++)
//                 tf_bytes[i] = 0;

//             task->tf->sepc = 0x0;              // entry at user VA 0x0
//             task->tf->sp = 0x4000000000UL;     // top of user stack
//             task->tf->sstatus = (1 << 5);      // SPIE=1, SPP=0 (user mode)

//             // --- 5. Kernel context for switch_to ---
//             task->thread.ra = (uint64_t)ret_from_exception;
//             task->thread.sp = (uint64_t)task->tf;

//             // --- 6. Schedule ---
//             disable_interrupt();
//             list_add_tail(&task->list, &runqueue);
//             enable_interrupt();

//             // printk("[exec] PID %d added, state=%d\n", task->pid, task->state);
//             uart_puts("[exec] PID "); uart_hex(task->pid); uart_puts(" scheduled\n");

//              // --- 7. Wait for the process to exit (become ZOMBIE) ---
//              disable_interrupt();
//              while (task->state != TASK_ZOMBIE) {
//                  schedule();
//              }
//             enable_interrupt();
//             while (task->state != TASK_ZOMBIE) {
//                 schedule();
//                 enable_interrupt();
//             }
//             enable_interrupt();
//             uart_puts("Process Exited. Returning to shell...\n");
//             return;
// }

//         uintptr_t next = align_up(content_start + filesize, 4);
//         p = (char *)next;
//     }
//     uart_puts("File not found: "); uart_puts(target_filename); uart_puts("\n");
// }


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
