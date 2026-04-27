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
unsigned long *pagewalk(unsigned long *pgd, unsigned long va, int alloc);
void map_pages(unsigned long *pgd, unsigned long va, unsigned long pa,
               unsigned long size, unsigned long flags);
void handle_page_fault(struct TrapFrame *tf, unsigned long exception_code);

unsigned long walk_page_table_to_get_phys(unsigned long *pgd, unsigned long va);
void clone_vmas_and_page_tables(struct task_struct *parent, struct task_struct *child);