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
#include "vfs.h"
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

static struct task_struct *create_user_task(uintptr_t content_start, unsigned long filesize) {
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
    INIT_LIST_HEAD(&task->mm.mmap_list);

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
        
        unsigned long prog_pa = virt_to_phys((unsigned long)prog_buf);
        
        // [FIX 1]: Added PAGE_WRITE so the program can modify its .data segment!
        map_pages(user_pgd, 0x0 + i * PAGE_SIZE,
                  prog_pa, PAGE_SIZE,
                  PAGE_PRESENT | PAGE_READ | PAGE_WRITE | PAGE_EXEC | PAGE_USER | PAGE_ACCESSED | PAGE_DIRTY);

        inc_page_ref(prog_pa);
    }

    // ==========================================================
    // --- 3. CREATE THE TEXT VMA WITH BSS PADDING ---
    // ==========================================================
    struct vm_area_struct *text_vma = (struct vm_area_struct *)kmalloc(sizeof(struct vm_area_struct));
    if (text_vma) {
        text_vma->vm_start = 0x0;
        text_vma->vm_end   = (text_pages * PAGE_SIZE) + (4 * PAGE_SIZE); 
        text_vma->vm_mm    = &task->mm;
        text_vma->vm_flags = VM_READ | VM_WRITE | VM_EXEC; 
        text_vma->vm_file  = 0;
        
        list_add_tail(&text_vma->list, &task->mm.mmap_list);
    }

    // ==========================================================
    // --- 4. CREATE THE STACK VMA (Lazy Promise) ---
    // ==========================================================
    // [FIX 2]: Restored the missing Stack VMA!
    struct vm_area_struct *stack_vma = (struct vm_area_struct *)kmalloc(sizeof(struct vm_area_struct));
    if (stack_vma) {
        stack_vma->vm_start = 0x3fffffc000UL;
        stack_vma->vm_end   = 0x4000000000UL; 
        stack_vma->vm_mm    = &task->mm;
        stack_vma->vm_flags = VM_READ | VM_WRITE | VM_EXEC;
        stack_vma->vm_file  = 0;
        
        list_add_tail(&stack_vma->list, &task->mm.mmap_list);
    }

    // --- 5. TrapFrame & Kernel Context ---
    task->tf = (struct TrapFrame *)(task->kernel_stack + PAGE_SIZE - sizeof(struct TrapFrame));
    memset(task->tf, 0, sizeof(struct TrapFrame));
    task->tf->sepc    = 0x0;
    task->tf->sp      = 0x4000000000UL;
    task->tf->sstatus = (1 << 5);

    task->thread.ra = (uint64_t)ret_from_exception;
    task->thread.sp = (uint64_t)task->tf;

    /* Wire up stdin/stdout/stderr (fd 0/1/2) to /dev/uart for the process. */
    vfs_setup_stdio(task->fd_table);

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
            disable_interrupt();
            uart_puts("Loading '"); uart_puts(filename); uart_puts("'...\n");
            enable_interrupt();
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
