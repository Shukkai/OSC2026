#include "vm.h"
#include "mm.h"
#include "uart.h"
#include "sched.h"
#define HPAGE_SIZE (1UL << 30)
#define HPAGE_NR   (HPAGE_SIZE / PAGE_SIZE)

unsigned long
    __attribute__((section(".data"), aligned(PAGE_SIZE))) pg_dir[512] = { 0 };

void setup_vm(void)
{
    for (int i = 0; i < 4; i++) {
        unsigned long pte = ((unsigned long)i * HPAGE_NR) << 10 |
                            PAGE_PRESENT | PAGE_READ | PAGE_WRITE |
                            PAGE_EXEC | PAGE_DIRTY | PAGE_ACCESSED;

        pg_dir[i] = pte;         /* Identity: 0..4GiB */
        pg_dir[256 + i] = pte;   /* Higher-half: 0xffffffc0_00000000 + 0..4GiB */
    }
    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        :
        : "r"(0x8UL << 60 | (unsigned long)pg_dir >> 12)
        : "memory"
    );
}

unsigned long *pagewalk(unsigned long *pgd, unsigned long va, int alloc)
{
    for (int level = 2; level > 0; level--) {
        int idx = (va >> (12 + 9 * level)) & 0x1FF;
        unsigned long *pte = &pgd[idx];

        if (*pte & PAGE_PRESENT) {
            unsigned long next_pa = (*pte >> 10) << 12;
            pgd = (unsigned long *)phys_to_virt(next_pa);
        } else {
            if (!alloc) return 0;
            unsigned long *new_pg = (unsigned long *)kmalloc(PAGE_SIZE);
            if (!new_pg) return 0;
            for (int i = 0; i < 512; i++) new_pg[i] = 0;
            unsigned long new_pa = virt_to_phys((unsigned long)new_pg);
            *pte = (new_pa >> 12) << 10 | PAGE_PRESENT;
            pgd = new_pg;
        }
    }

    return &pgd[(va >> 12) & 0x1FF];
}

void map_pages(unsigned long *pgd, unsigned long va, unsigned long pa,
               unsigned long size, unsigned long flags)
{
    for (unsigned long off = 0; off < size; off += PAGE_SIZE) {
        unsigned long *pte = pagewalk(pgd, va + off, 1);
        if (pte)
            *pte = ((pa + off) >> 12) << 10 | flags;
    }
}


extern void do_exit(int status);


void handle_page_fault(struct TrapFrame *tf, unsigned long exception_code) {
    unsigned long fault_addr = tf->stval;
    unsigned long page_addr = fault_addr & ~(PAGE_SIZE - 1); 

    // [DEBUG] Print fault info
    // uart_puts("\n[DEBUG] FAULT! Addr: "); uart_hex(fault_addr);
    // uart_puts(" Code: "); uart_hex(exception_code);
    // uart_puts(" PC: "); uart_hex(tf->sepc); uart_puts("\n");

    // 1. Check the VMA list
    struct vm_area_struct *vma = NULL;
    struct list_head *pos;
    list_for_each(pos, &current->mm.mmap_list) {
        struct vm_area_struct *v = list_entry(pos, struct vm_area_struct, list);
        if (fault_addr >= v->vm_start && fault_addr < v->vm_end) {
            vma = v;
            break;
        }
    }

    if (!vma) {
        uart_puts("<segmentation fault: unmapped region>\n");
        do_exit(-1); 
        return; 
    }

    // 2. Check if the physical memory ALREADY exists
    unsigned long old_phys = walk_page_table_to_get_phys(current->mm.pgd, page_addr);

    // ==========================================
    // SCENARIO A: COPY-ON-WRITE FAULT
    // ==========================================
    if (old_phys && exception_code == 15) {
        if (!(vma->vm_flags & VM_WRITE)) {
            uart_puts("<segmentation fault: write to read-only region>\n");
            do_exit(-1);
            return; 
        }

        uart_puts("[Kernel] Copy-On-Write triggered!\n");

        // 1. Allocate a brand new physical page
        char *new_page = (char *)kmalloc(PAGE_SIZE);
        if (!new_page) { do_exit(-1); return; }

        // 2. Copy the exact data from the old shared page to the new private page
        char *src_page = (char *)phys_to_virt(old_phys);
        for (unsigned long i = 0; i < PAGE_SIZE; i++) {
            new_page[i] = src_page[i];
        }

        // 3. Map the new page INSTEAD of the old one, WITH WRITE PERMISSIONS
        unsigned long new_phys = virt_to_phys((unsigned long)new_page);
        unsigned long map_flags = PAGE_PRESENT | PAGE_USER | PAGE_ACCESSED | PAGE_DIRTY | PAGE_READ | PAGE_WRITE;
        if (vma->vm_flags & VM_EXEC) map_flags |= PAGE_EXEC;

        map_pages(current->mm.pgd, page_addr, new_phys, PAGE_SIZE, map_flags);

        // 4. Update the reference counts!
        inc_page_ref(new_phys);
        dec_page_ref(old_phys); // If parent dies, this drops to 0 and frees!

        asm volatile("sfence.vma zero, zero");
        return;
    }

    // ==========================================
    // SCENARIO B: DEMAND PAGING FAULT
    // ==========================================
    if (!old_phys) {
        if (exception_code == 15 && !(vma->vm_flags & VM_WRITE)) {
            uart_puts("<segmentation fault>\n");
            do_exit(-1); return; 
        }

        uart_puts("[Kernel] Demand Paging allocated a new page!\n");

        char *pa_mem = (char *)kmalloc(PAGE_SIZE);
        if (!pa_mem) { do_exit(-1); return; }
        for (unsigned long i = 0; i < PAGE_SIZE; i++) pa_mem[i] = 0;

        unsigned long phys_addr = virt_to_phys((unsigned long)pa_mem);
        unsigned long map_flags = PAGE_PRESENT | PAGE_USER | PAGE_ACCESSED | PAGE_DIRTY;
        if (vma->vm_flags & VM_READ)  map_flags |= PAGE_READ;
        if (vma->vm_flags & VM_WRITE) map_flags |= PAGE_WRITE;
        if (vma->vm_flags & VM_EXEC)  map_flags |= PAGE_EXEC;

        map_pages(current->mm.pgd, page_addr, phys_addr, PAGE_SIZE, map_flags);

        // [NEW]: We mapped a new page, increment its reference!
        inc_page_ref(phys_addr);

        asm volatile("sfence.vma zero, zero");
        return;
    }
}

unsigned long walk_page_table_to_get_phys(unsigned long *pgd, unsigned long va) {
    for (int level = 2; level > 0; level--) {
        int idx = (va >> (12 + 9 * level)) & 0x1FF;
        unsigned long *pte = &pgd[idx];
        if (!(*pte & PAGE_PRESENT)) return 0; // Not mapped!
        pgd = (unsigned long *)phys_to_virt((*pte >> 10) << 12);
    }
    unsigned long *pte = &pgd[(va >> 12) & 0x1FF];
    if (*pte & PAGE_PRESENT) {
        return (*pte >> 10) << 12; // Return the physical address
    }
    return 0; // Not mapped!
}


void clone_vmas_and_page_tables(struct task_struct *parent, struct task_struct *child) {
    INIT_LIST_HEAD(&child->mm.mmap_list);
    
    struct list_head *pos;
    list_for_each(pos, &parent->mm.mmap_list) {
        struct vm_area_struct *vma = list_entry(pos, struct vm_area_struct, list);
        
        // 1. Clone the VMA promise
        struct vm_area_struct *new_vma = (struct vm_area_struct *)kmalloc(sizeof(struct vm_area_struct));
        *new_vma = *vma; // Copy all boundaries and flags
        new_vma->vm_mm = &child->mm;
        list_add_tail(&new_vma->list, &child->mm.mmap_list);

        // 2. Clone the actual Page Table entries
        for (unsigned long va = vma->vm_start; va < vma->vm_end; va += PAGE_SIZE) {
            unsigned long *parent_pte = pagewalk(parent->mm.pgd, va, 0); // alloc=0
            
            // If the parent actually has physical memory mapped here...
            if (parent_pte && (*parent_pte & PAGE_PRESENT)) {
                unsigned long phys_addr = (*parent_pte >> 10) << 12;
                unsigned long flags = *parent_pte & 0x3FF;

                // COPY-ON-WRITE MAGIC: If it is writable, lock it down to Read-Only!
                if (flags & PAGE_WRITE) {
                    flags &= ~PAGE_WRITE; // Strip the write permission
                    *parent_pte = (phys_addr >> 12) << 10 | flags; // Update Parent PTE
                }

                // Map it into the child with the exact same Read-Only flags
                map_pages(child->mm.pgd, va, phys_addr, PAGE_SIZE, flags);
                
                // Increment the physical memory reference count!
                inc_page_ref(phys_addr);
            }
        }
    }
    
    // Flush the TLB since we secretly changed the parent's permissions to Read-Only!
    asm volatile("sfence.vma zero, zero");
}