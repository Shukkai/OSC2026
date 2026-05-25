#pragma once

#include "list.h"
struct TrapFrame;   // Defined in trap.h
struct task_struct; // Defined in sched.h
#define PAGE_OFFSET   0xffffffc000000000UL
#define HPAGE_NR      (HPAGE_SIZE / PAGE_SIZE)

#define MMAP_BASE     0x10000000UL

/* Page protection bits */
#define PAGE_PRESENT  (1UL << 0)
#define PAGE_READ     (1UL << 1)
#define PAGE_WRITE    (1UL << 2)
#define PAGE_EXEC     (1UL << 3)
#define PAGE_USER     (1UL << 4)
#define PAGE_GLOBAL   (1UL << 5)
#define PAGE_ACCESSED (1UL << 6)
#define PAGE_DIRTY    (1UL << 7)

#define PAGE_KERNEL   (PAGE_PRESENT | PAGE_READ | PAGE_WRITE | \
                       PAGE_EXEC | PAGE_GLOBAL | PAGE_ACCESSED | PAGE_DIRTY)

#define SATP_MODE_SV39 (8UL << 60)

/* VM flags for vm_area_struct */
#define VM_NONE   0x0
#define VM_READ   0x1
#define VM_WRITE  0x2
#define VM_EXEC   0x4

/* mmap protections */
#define PROT_NONE     0x0
#define PROT_READ     0x1
#define PROT_WRITE    0x2
#define PROT_EXEC     0x4
#define MAP_ANONYMOUS 0x20
#define MAP_POPULATE  0x8000

/* ------------------------------------------------------------------ */
/* Page-size constants                                                */
/* ------------------------------------------------------------------ */
#define PAGE_SHIFT    12
#define PMD_SHIFT     21                       /* 2 MiB pages          */
#define PGD_SHIFT     30                       /* 1 GiB pages          */
#define MPAGE_SIZE    (1UL << PMD_SHIFT)       /* 2 MiB                */
#define HPAGE_SIZE    (1UL << PGD_SHIFT)       /* 1 GiB                */
#define HPAGE_NR      (HPAGE_SIZE / PAGE_SIZE)

/* ------------------------------------------------------------------ */
/* PTE helpers                                                        */
/* ------------------------------------------------------------------ */
/* In RISC-V SV39, a PTE is "leaf" if any of R/W/X is set, otherwise
 * it points at the next-level table. We define two flag presets:      */
#define PAGE_VALID    PAGE_PRESENT             /* non-leaf pointer PTE */
#define PAGE_MMIO     (PAGE_PRESENT | PAGE_READ | PAGE_WRITE | \
                       PAGE_GLOBAL  | PAGE_ACCESSED | PAGE_DIRTY)
                                               /* RW, no-exec, global  */

#define PA_TO_PTE(pa, flags)  ((((unsigned long)(pa)) >> 12) << 10 | (flags))
#define GET_PPN(va)           (((unsigned long)(va) - PAGE_OFFSET) >> 12)

#define VA_PGD_IDX(va)        (((unsigned long)(va) >> PGD_SHIFT) & 0x1FF)
#define VA_PMD_IDX(va)        (((unsigned long)(va) >> PMD_SHIFT) & 0x1FF)
#define VA_PTE_IDX(va)        (((unsigned long)(va) >> PAGE_SHIFT) & 0x1FF)

#define ALIGN_DOWN(x, a)      ((unsigned long)(x) & ~((unsigned long)(a) - 1))

#define virt_to_phys(x) ((unsigned long)(x) - PAGE_OFFSET)
#define phys_to_virt(x) ((unsigned long)(x) + PAGE_OFFSET)

struct mm_struct {
    unsigned long *pgd;
    struct list_head mmap_list;
};

struct vm_area_struct {
    unsigned long vm_start;
    unsigned long vm_end;
    struct mm_struct *vm_mm;
    unsigned long vm_flags;
    unsigned long vm_file;
    struct list_head list;
};

void setup_vm(void);
void drop_identity_map(void);
void clone_kernel_pgd(unsigned long *new_pgd);
unsigned long *pagewalk(unsigned long *pgd, unsigned long va, int alloc);
void map_pages(unsigned long *pgd, unsigned long va, unsigned long pa,
               unsigned long size, unsigned long flags);
void handle_page_fault(struct TrapFrame *tf, unsigned long exception_code);

unsigned long walk_page_table_to_get_phys(unsigned long *pgd, unsigned long va);
void clone_vmas_and_page_tables(struct task_struct *parent, struct task_struct *child);
void drop_identity_map(void);