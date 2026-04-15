#include "vm.h"
#include "mm.h"
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