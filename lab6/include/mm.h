/* include/mm.h */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "list.h"

// =========================================================================
// Constants & Macros
// =========================================================================
#define PAGE_SHIFT      12
#define PAGE_SIZE       (1UL << PAGE_SHIFT)
#define MAX_ORDER       19                

// [FIX]: Added back the missing macro for your test cases
#define MAX_ALLOC_SIZE  (PAGE_SIZE << MAX_ORDER)

// Page Status Flags
#define PAGE_FREE        0 
#define PAGE_ALLOCATED  -1
#define PAGE_RESERVED   -2 
#define PAGE_INTERNAL   -3

// Cache Index Flags
#define CACHE_NONE      -1

// =========================================================================
// Dynamic Memory Allocator (SLAB) Definitions
// =========================================================================
#define CACHE_NUM 8

struct kmem_cache {
    struct list_head free_list;
    unsigned int chunk_size;
};

// =========================================================================
// Physical Memory & Page Structures
// =========================================================================
extern unsigned long PHY_MEM_START;
extern unsigned long TOTAL_MEM_SIZE;

// [FIX]: Added back buddy_verbose so cmd.c can toggle debug logs
extern int buddy_verbose;

extern char _start[]; 
extern char _end[];   

struct page {
    struct list_head list; 
    int order;             
    int status;
    int cache_index;  
    int ref_count;         
};

struct free_area {
    struct list_head free_list;
    unsigned long nr_free;
};

// =========================================================================
// Core Memory API
// =========================================================================
void mm_init(void *dtb);
void *kmalloc(size_t size);
void kfree(void *ptr);

// Page Frame Allocator API
struct page *alloc_pages(int order);
void free_pages(struct page *page, int order);
void inc_page_ref(unsigned long phys_addr);
void dec_page_ref(unsigned long phys_addr);

// Startup Allocator API
void bootmem_init(unsigned long safe_start_pa);
void *bootmem_alloc(unsigned long size);

// Reserved Memory API
void memory_reserve(unsigned long start, unsigned long size);

// Helpers
unsigned long page_to_pfn(struct page *page);
void *page_to_virt(struct page *page);
struct page *virt_to_page(void *addr);
