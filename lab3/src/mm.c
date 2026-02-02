/* src/mm.c */
#include "mm.h"
#include "uart.h"
#include "utils.h"
#include "fdt.h" 

// If fdt.h is missing this prototype, declare it here to fix warning
extern uint32_t fdt_totalsize(const void *fdt);

// =========================================================================
// Global Variables & SLAB Definitions
// =========================================================================

// --- SLAB Allocator Config ---
#define CACHE_NUM 8
static unsigned int cache_sizes[CACHE_NUM] = {16, 32, 64, 128, 256, 512, 1024, 2048};

struct kmem_cache {
    struct list_head free_list;
    unsigned int chunk_size;
};

struct kmem_cache caches[CACHE_NUM];

// --- Buddy System Globals ---
struct page *mem_map;             
struct free_area free_areas[MAX_ORDER + 1];
unsigned long total_pages;
unsigned long total_memory_size; 
int buddy_verbose = 0; 

// =========================================================================
// Helpers
// =========================================================================
unsigned long page_to_pfn(struct page *page) {
    return page - mem_map;
}

void *page_to_virt(struct page *page) {
    return (void *)(PHY_MEM_START + (page_to_pfn(page) << PAGE_SHIFT));
}

struct page *virt_to_page(void *addr) {
    unsigned long pfn = ((unsigned long)addr - PHY_MEM_START) >> PAGE_SHIFT;
    if (pfn >= total_pages) return NULL;
    return &mem_map[pfn];
}

// =========================================================================
// Startup Allocator (Bump Allocator)
// =========================================================================
static uintptr_t bootmem_ptr = 0;

void *bootmem_alloc(unsigned long size) {
    // Initialize bootmem_ptr to the end of the kernel if not set
    if (bootmem_ptr == 0) {
        bootmem_ptr = align_up((uintptr_t)_end, PAGE_SIZE);
    }
    
    uintptr_t alloc_start = bootmem_ptr;
    bootmem_ptr = align_up(bootmem_ptr + size, PAGE_SIZE);
    
    return (void *)alloc_start;
}

// =========================================================================
// Reserved Memory Logic
// =========================================================================
void memory_reserve(unsigned long start, unsigned long size) {
    unsigned long start_addr = start;
    unsigned long end_addr = start + size;

    unsigned long start_pfn = (start_addr - PHY_MEM_START) >> PAGE_SHIFT;
    unsigned long end_pfn = (align_up(end_addr, PAGE_SIZE) - PHY_MEM_START) >> PAGE_SHIFT;

    if (end_pfn > total_pages) end_pfn = total_pages;

    uart_puts("Reserving: "); uart_hex(start_addr); 
    uart_puts(" - "); uart_hex(end_addr); uart_puts("\n");

    for (unsigned long i = start_pfn; i < end_pfn; i++) {
        if (mem_map[i].status != PAGE_RESERVED) {
            mem_map[i].status = PAGE_RESERVED;
            mem_map[i].order = 0; 
            mem_map[i].cache_index = CACHE_NONE;
        }
    }
}

// =========================================================================
// Initialization (mm_init)
// =========================================================================
void mm_init(void *dtb) {
    uart_puts("Initializing Memory System...\n");

    // 1. Get Memory Size
    #ifdef __QEMU__
        total_memory_size = 0x08000000; // 128MB
    #else
        total_memory_size = 0x40000000; // 1GB
    #endif
    
    total_pages = total_memory_size / PAGE_SIZE;

    // 2. Allocate Page Descriptors using Startup Allocator
    unsigned long mem_map_size = total_pages * sizeof(struct page);
    mem_map = (struct page *)bootmem_alloc(mem_map_size);
    
    uart_puts("mem_map allocated at: "); uart_hex((unsigned long)mem_map); uart_puts("\n");

    // 3. Initialize all pages as FREE first
    for (unsigned long i = 0; i < total_pages; i++) {
        list_init(&mem_map[i].list);
        mem_map[i].status = PAGE_FREE; 
        mem_map[i].order = 0;
        mem_map[i].cache_index = CACHE_NONE; 
    }

    // 4. Initialize Buddy Lists
    for (int i = 0; i <= MAX_ORDER; i++) {
        list_init(&free_areas[i].free_list); 
        free_areas[i].nr_free = 0;
    }

    // 5. Initialize SLAB Caches
    for (int i = 0; i < CACHE_NUM; i++) {
        list_init(&caches[i].free_list);
        caches[i].chunk_size = cache_sizes[i];
    }

    // // ======================================================
    // // RESERVATIONS
    // // ======================================================
    uart_puts("--- Performing Memory Reservations ---\n");

    // // A. Reserve OS Core (OpenSBI + Kernel + Stack + mem_map)
    // // Covers: OpenSBI (0x40000000), Kernel, Stack, and Page Descriptors
    uart_puts("[Reserve] OS Core (OpenSBI, Kernel, Stack, mem_map)...\n"); 
    // memory_reserve(PHY_MEM_START, (unsigned long)bootmem_ptr - PHY_MEM_START);

    // B. Reserve Device Tree Blob (DTB)
    if (dtb) {
        uart_puts("[Reserve] Device Tree Blob (DTB)...\n"); 
        uint32_t dtb_size = fdt_totalsize(dtb);
        memory_reserve((unsigned long)dtb, dtb_size);
    }

    // C. Reserve Initramfs
    if (dtb) {
        int chosen_node = fdt_path_offset(dtb, "/chosen");
        if (chosen_node >= 0) {
            int len_start, len_end;
            const uint32_t *prop_start = fdt_getprop(dtb, chosen_node, "linux,initrd-start", &len_start);
            const uint32_t *prop_end = fdt_getprop(dtb, chosen_node, "linux,initrd-end", &len_end);
            
            if (prop_start && prop_end) {
                uart_puts("[Reserve] Initial Ramdisk (Initrd)...\n");

                uint64_t initrd_start = 0;
                uint64_t initrd_end = 0;

                if (len_start == 8) {
                    uint64_t hi = bswap32(prop_start[0]);
                    uint64_t lo = bswap32(prop_start[1]);
                    initrd_start = (hi << 32) | lo;
                } else {
                    initrd_start = bswap32(*prop_start);
                }

                if (len_end == 8) {
                    uint64_t hi = bswap32(prop_end[0]);
                    uint64_t lo = bswap32(prop_end[1]);
                    initrd_end = (hi << 32) | lo;
                } else {
                    initrd_end = bswap32(*prop_end);
                }

                memory_reserve(initrd_start, initrd_end - initrd_start);
            }
        }
    }

    // D. Reserve Hardware/Firmware Regions (Memory Reservation Block)
    if (dtb) {
        uart_puts("[Reserve] /memreserve/ Block (Firmware)...\n");
        struct fdt_header *header = (struct fdt_header *)dtb;
        
        // Get offset of the reservation block
        uint32_t off = bswap32(header->off_mem_rsvmap);
        struct fdt_reserve_entry *entry = (struct fdt_reserve_entry *)((void *)dtb + off);
        
        // Iterate until both address and size are 0
        while (entry->address != 0 || entry->size != 0) {
            uint64_t rsv_addr = bswap64(entry->address);
            uint64_t rsv_size = bswap64(entry->size);
            
            memory_reserve(rsv_addr, rsv_size);
            
            entry++;
        }
    }

    // E. Reserve Frame Buffer (Simple Framebuffer)
    // We reserve the top 8MB of RAM for potential display use
    {
        unsigned long fb_size = 0x00800000; // 8 MB
        unsigned long fb_start = PHY_MEM_START + total_memory_size - fb_size;
        
        uart_puts("[Reserve] Frame Buffer (Top 8MB)...\n");
        memory_reserve(fb_start, fb_size);
    }

    // ======================================================
    // Handover to Buddy System
    // ======================================================
    uart_puts("Handing over to Buddy System...\n");

    for (unsigned long i = 0; i < total_pages; i++) {
        if (mem_map[i].status == PAGE_FREE) {
            mem_map[i].status = PAGE_ALLOCATED; 
            free_pages(&mem_map[i], 0);
        }
    }
    uart_puts("Buddy System Ready.\n");
}

// =========================================================================
// Buddy System & SLAB Implementation (Keep existing code below)
// =========================================================================
struct page *alloc_pages(int order) {
    int current_order = order;
    while (current_order <= MAX_ORDER) {
        if (!list_empty(&free_areas[current_order].free_list)) {
            struct list_head *entry = free_areas[current_order].free_list.next;
            struct page *page = (struct page *)entry; 
            list_del(entry); 
            free_areas[current_order].nr_free--;
            while (current_order > order) {
                current_order--;
                unsigned long pfn = page_to_pfn(page);
                unsigned long buddy_pfn = pfn + (1UL << current_order);
                struct page *buddy = &mem_map[buddy_pfn];
                buddy->order = current_order;
                buddy->status = PAGE_FREE;
                list_add(&buddy->list, &free_areas[current_order].free_list); 
                free_areas[current_order].nr_free++;
                if (buddy_verbose) { 
                    uart_puts("[Buddy] Split: PFN "); uart_hex(pfn);
                    uart_puts(" order "); uart_hex(current_order + 1);
                    uart_puts(" -> Buddy "); uart_hex(buddy_pfn); 
                    uart_puts("\n");
                }
            }
            page->status = PAGE_ALLOCATED;
            page->order = order;
            page->cache_index = CACHE_NONE; 
            return page;
        }
        current_order++;
    }
    return NULL;
}

void free_pages(struct page *page, int order) {
    if (page == NULL) return;
    unsigned long pfn = page_to_pfn(page);
    int current_order = order;
    if (page->status != PAGE_ALLOCATED) return;
    page->status = PAGE_FREE;
    while (current_order < MAX_ORDER) {
        unsigned long buddy_pfn = pfn ^ (1UL << current_order);
        if (buddy_pfn >= total_pages) break; 
        struct page *buddy = &mem_map[buddy_pfn];
        if (buddy->status == PAGE_FREE && buddy->order == current_order) {
            if (buddy_verbose) {
                uart_puts("[Buddy] Merge: PFN "); uart_hex(pfn);
                uart_puts(" and Buddy "); uart_hex(buddy_pfn);
                uart_puts("\n");
            }
            list_del(&buddy->list); 
            free_areas[current_order].nr_free--;
            if (buddy_pfn < pfn) {
                pfn = buddy_pfn;
                page = buddy;
            }
            current_order++;
            page->order = current_order;
        } else {
            break;
        }
    }
    page->order = current_order;
    page->status = PAGE_FREE;
    list_add(&page->list, &free_areas[current_order].free_list); 
    free_areas[current_order].nr_free++;
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    if (size > 2048) {
        int order = 0;
        while (((size_t)PAGE_SIZE << order) < size) {
            order++;
        }
        struct page *p = alloc_pages(order);
        if (!p) return NULL;
        p->cache_index = CACHE_NONE; 
        return page_to_virt(p);
    }
    int cache_idx = 0;
    while (cache_idx < CACHE_NUM && cache_sizes[cache_idx] < size) {
        cache_idx++;
    }
    if (cache_idx >= CACHE_NUM) return NULL; 
    struct kmem_cache *cache = &caches[cache_idx];
    if (!list_empty(&cache->free_list)) {
        struct list_head *ptr = cache->free_list.next;
        list_del(ptr);
        return (void *)ptr;
    }
    struct page *page = alloc_pages(0); 
    if (!page) return NULL;
    page->cache_index = cache_idx; 
    void *page_addr = page_to_virt(page);
    unsigned int chunk_size = cache->chunk_size;
    for (unsigned int offset = chunk_size; offset + chunk_size <= PAGE_SIZE; offset += chunk_size) {
        struct list_head *chunk = (struct list_head *)((char *)page_addr + offset);
        list_add(chunk, cache->free_list.prev);
    }
    return page_addr; 
}

void kfree(void *ptr) {
    if (!ptr) return;
    struct page *page = virt_to_page(ptr);
    if (!page) return;
    if (page->cache_index == CACHE_NONE) {
        free_pages(page, page->order);
        return;
    }
    int cache_idx = page->cache_index;
    if (cache_idx < 0 || cache_idx >= CACHE_NUM) {
        uart_puts("[Slab] Error: Corrupt cache index\n");
        return;
    }
    struct kmem_cache *cache = &caches[cache_idx];
    struct list_head *node = (struct list_head *)ptr;
    list_add(node, &cache->free_list);
}