/* src/mm.h */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "list.h"

#define PAGE_SHIFT      12
#define PAGE_SIZE       (1 << PAGE_SHIFT) 
#define MAX_ORDER       10                

// Page Status Flags
#define PAGE_FREE        0 
#define PAGE_ALLOCATED  -1
#define PAGE_RESERVED   -2 
#define PAGE_INTERNAL   -3

// Cache Index Flags
#define CACHE_NONE      -1

// External symbols from linker script
extern char _start[]; 
extern char _end[];   
extern int buddy_verbose;

// Physical memory layout
#ifdef __QEMU__
    #define PHY_MEM_START   0x80200000
#else
    #define PHY_MEM_START   0x00200000 
#endif

struct page {
    struct list_head list; 
    int order;             
    int status;
    int cache_index;       
};

struct free_area {
    struct list_head free_list;
    unsigned long nr_free;
};

// Core API
void mm_init(void *dtb);
void *kmalloc(size_t size);
void kfree(void *ptr);
struct page *alloc_pages(int order);
void free_pages(struct page *page, int order);

// Advanced Exercise API
void memory_reserve(unsigned long start, unsigned long size);
void *bootmem_alloc(unsigned long size);

// Helpers needed by cmd.c
unsigned long page_to_pfn(struct page *page);
void *page_to_virt(struct page *page);