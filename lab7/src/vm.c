#include "vm.h"
#include "mm.h"
#include "uart.h"
#include "sched.h"



/* ------------------------------------------------------------------ */
/* Statically-allocated paging structures                             */
/*                                                                    */
/*   pg_dir         : top-level PGD (256 GiB / entry)                 */
/*   pmd_kern[i]    : PMD for higher-half kernel window (PA i*1GiB)   */
/*   pmd_id[i]      : PMD for identity 0..4GiB (dropped after boot)   */
/*   pte_uart       : PTE for the 2 MiB block containing UART_BASE    */
/* ------------------------------------------------------------------ */
/* GiB of physical RAM mapped into the higher-half kernel window.       */
/* Must cover the board's top of RAM (Orange Pi RV2 ships up to 8 GiB). */
/* The temporary identity map below stays at 4 GiB — it only needs to   */
/* cover the kernel load address during boot and is dropped afterwards. */
#define KERN_MAP_GIB 8

unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pg_dir[512] = { 0 };

static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pmd_kern[KERN_MAP_GIB][512] = { { 0 } };

static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pmd_id[4][512] = { { 0 } };

static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pte_uart[512] = { 0 };

/* ------------------------------------------------------------------ */
/* setup_vm: build the kernel page table & enable the MMU             */
/*                                                                    */
/* IMPORTANT: this runs at the kernel's PHYSICAL load address, with   */
/* the MMU off. Although symbols like `pmd_kern` are LINKED at        */
/* higher-half VAs, `-mcmodel=medany` resolves them via PC-relative   */
/* `auipc`, so taking their address here yields the actual PA. Do NOT */
/* run them through virt_to_phys() — that would subtract PAGE_OFFSET  */
/* from a small PA and underflow.                                      */
/* ------------------------------------------------------------------ */
void setup_vm(void)
{
    /*
     * Step 1a: higher-half kernel window. Fill KERN_MAP_GIB PMD tables,
     * each describing 1 GiB of physical memory in 2 MiB strides, so the
     * whole of the board's RAM is reachable via phys_to_virt(). This is
     * what lets kmalloc hand out pages above 4 GiB on real hardware.
     */
    for (int i = 0; i < KERN_MAP_GIB; i++) {
        for (unsigned long j = 0; j < 512; j++) {
            unsigned long pa = ((unsigned long)i << PGD_SHIFT)
                             + (j * MPAGE_SIZE);
            pmd_kern[i][j] = PA_TO_PTE(pa, PAGE_KERNEL);
        }
        /* PC-relative `auipc` => `(unsigned long)pmd_kern[i]` IS a PA */
        pg_dir[256 + i] = PA_TO_PTE((unsigned long)pmd_kern[i], PAGE_VALID);
    }

    /*
     * Step 1b: temporary identity map, first 4 GiB only. It just has to
     * cover the kernel load address (and page tables) while the MMU is
     * brought up; drop_identity_map() tears it down once the PC is in
     * higher-half VA space.
     */
    for (int i = 0; i < 4; i++) {
        for (unsigned long j = 0; j < 512; j++) {
            unsigned long pa = ((unsigned long)i << PGD_SHIFT)
                             + (j * MPAGE_SIZE);
            pmd_id[i][j] = PA_TO_PTE(pa, PAGE_KERNEL);
        }
        pg_dir[i] = PA_TO_PTE((unsigned long)pmd_id[i], PAGE_VALID);
    }

    /*
     * Step 2: replace the single 2 MiB PMD entry covering the UART
     * with a pointer to a 4 KiB PTE table, so we can mark just the
     * MMIO page as non-executable while leaving the rest of that
     * 2 MiB block as ordinary kernel RAM.
     *
     * DEFAULT_UART_BASE (from uart.h) is the *physical* address of
     * the UART. We can't consult the DTB-derived `uart_base_addr`
     * here because the DTB is parsed after the MMU is enabled.
     * The default suffices: 0x10000000 (QEMU) and 0xd4017000 (OPI)
     * both fall within our identity/higher-half-mapped 4 GiB window.
     */
    unsigned long uart_pa  = DEFAULT_UART_BASE;        /* PA of UART  */
    int pgd_slot           = uart_pa >> PGD_SHIFT;     /* which 1 GiB */
    int pmd_slot           = (uart_pa >> PMD_SHIFT) & 0x1FF;

    unsigned long block_pa  = ALIGN_DOWN(uart_pa, MPAGE_SIZE);
    unsigned long uart_page = uart_pa & ~(PAGE_SIZE - 1);
    for (int i = 0; i < 512; i++) {
        unsigned long pa = block_pa + ((unsigned long)i * PAGE_SIZE);
        if (pa == uart_page)
            pte_uart[i] = PA_TO_PTE(pa, PAGE_MMIO);    /* RW, no-X    */
        else
            pte_uart[i] = PA_TO_PTE(pa, PAGE_KERNEL);  /* normal RAM  */
    }

    /* `(unsigned long)pte_uart` is also a PA at this point.           */
    pmd_kern[pgd_slot][pmd_slot] =
        PA_TO_PTE((unsigned long)pte_uart, PAGE_VALID);
    pmd_id[pgd_slot][pmd_slot]   =
        PA_TO_PTE((unsigned long)pte_uart, PAGE_VALID);

    /* Step 3: enable the MMU. `(unsigned long)pg_dir` is a PA here.   */
    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        :
        : "r"(SATP_MODE_SV39 | ((unsigned long)pg_dir >> 12))
        : "memory"
    );
}

/* ------------------------------------------------------------------ */
/* drop_identity_map: tear down the temporary 0..4GiB identity map.   */
/* MUST be called only after PC has jumped into higher-half VA space. */
/* ------------------------------------------------------------------ */
void drop_identity_map(void)
{
    for (int i = 0; i < 4; i++)
        pg_dir[i] = 0;
    asm volatile("sfence.vma zero, zero" ::: "memory");
}

/* ------------------------------------------------------------------ */
/* clone_kernel_pgd: copy the higher-half kernel mappings into a      */
/* fresh per-process PGD. Call this when allocating child->mm.pgd.    */
/* This is what was missing and caused fork to hang on QEMU.          */
/* ------------------------------------------------------------------ */
void clone_kernel_pgd(unsigned long *new_pgd)
{
    for (int i = 0; i < 512; i++) new_pgd[i] = 0;
    for (int i = 256; i < 256 + KERN_MAP_GIB; i++) new_pgd[i] = pg_dir[i];
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
        uart_puts("[Segmentation fault]: Kill Process\n");
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
            uart_puts("[Segmentation fault]: Kill Process\n");
            do_exit(-1);
            return; 
        }

        uart_puts("[Permission fault]: ");
        uart_hex(fault_addr);
        uart_puts("\n");

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
        if (exception_code == 13 && !(vma->vm_flags & VM_READ)) {
        uart_puts("[Segmentation fault]: Kill Process\n");
        do_exit(-1); return;
        }
        if (exception_code == 15 && !(vma->vm_flags & VM_WRITE)) {
            uart_puts("[Segmentation fault]: Kill Process\n");
            do_exit(-1); return;
        }
        if (exception_code == 12 && !(vma->vm_flags & VM_EXEC)) {
            uart_puts("[Segmentation fault]: Kill Process\n");
            do_exit(-1); return;
        }

        uart_puts("[Translation fault]: ");
        uart_hex(fault_addr);
        uart_puts("\n");

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
