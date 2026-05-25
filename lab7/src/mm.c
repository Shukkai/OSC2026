/* src/mm.c */
#include "mm.h"
#include "uart.h"
#include "utils.h"
#include "fdt.h" 
#include "vm.h"
#include "printk.h"
#include "string.h"
// If fdt.h is missing this prototype, declare it here to fix warning
extern uint32_t fdt_totalsize(const void *fdt);

// =========================================================================
// Global Variables & SLAB Definitions
// =========================================================================
// Internal SLAB configuration
static unsigned int cache_sizes[CACHE_NUM] = {16, 32, 64, 128, 256, 512, 1024, 2048};
struct kmem_cache caches[CACHE_NUM];

// Buddy System Globals
struct page *mem_map;             
struct free_area free_areas[MAX_ORDER + 1];
unsigned long total_pages;
unsigned long total_memory_size; 
int buddy_verbose = 0; 

unsigned long PHY_MEM_START = 0;
unsigned long TOTAL_MEM_SIZE = 0;

static uintptr_t bootmem_ptr = 0;

// =========================================================================
// Helpers
// =========================================================================
unsigned long page_to_pfn(struct page *page) {
    return page - mem_map;
}


void *page_to_virt(struct page *page) {
    unsigned long pfn = page_to_pfn(page);
    unsigned long pa = PHY_MEM_START + (pfn << PAGE_SHIFT);
    unsigned long va = phys_to_virt(pa);
    return (void *)va;
}
struct page *virt_to_page(void *addr) {
    unsigned long pa = virt_to_phys((unsigned long)addr);
    unsigned long pfn = (pa - PHY_MEM_START) >> PAGE_SHIFT;
    if (pfn >= total_pages) return NULL;
    return &mem_map[pfn];
}

// =========================================================================
// Startup Allocator (Bump Allocator)
// =========================================================================
// New helper to initialize the bump pointer at a safe location
void bootmem_init(unsigned long safe_start_pa) {
    bootmem_ptr = align_up(safe_start_pa, PAGE_SIZE);
}

void *bootmem_alloc(unsigned long size) {
    if (bootmem_ptr == 0) {
        // Fallback to kernel end if not explicitly initialized
        bootmem_ptr = align_up(virt_to_phys((uintptr_t)_end), PAGE_SIZE);
    }
    
    uintptr_t alloc_start_pa = bootmem_ptr;
    bootmem_ptr = align_up(bootmem_ptr + size, PAGE_SIZE);
    
    // Return the virtual address for kernel use
    return (void *)phys_to_virt(alloc_start_pa);
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

    // ======================================================
    // 1. DETECT RAM BOUNDARIES (DYNAMIC)
    // ======================================================
    if (dtb) {
        int mem_node = -1, depth = -1;
        int offset = fdt_next_node(dtb, -1, &depth);
        while (offset >= 0) {
            int len;
            const char *device_type = (const char *)fdt_getprop(dtb, offset, "device_type", &len);
            if (device_type && strcmp(device_type, "memory") == 0) {
                mem_node = offset;
                break; 
            }
            offset = fdt_next_node(dtb, offset, &depth);
        }

        if (mem_node >= 0) {
            int len;
            const uint32_t *reg = fdt_getprop(dtb, mem_node, "reg", &len);
            if (reg && len >= 16) { 
                PHY_MEM_START = ((uint64_t)bswap32(reg[0]) << 32) | bswap32(reg[1]);
                TOTAL_MEM_SIZE = ((uint64_t)bswap32(reg[2]) << 32) | bswap32(reg[3]);
            }
        }
    }

    if (TOTAL_MEM_SIZE == 0) {
        uart_puts("Failed to read memory from DTB. Halting.\n");
        while(1);
    }

    total_memory_size = TOTAL_MEM_SIZE;
    total_pages = total_memory_size / PAGE_SIZE;

    uart_puts("[mm] PHY_MEM_START="); uart_hex(PHY_MEM_START);
    uart_puts(" SIZE="); uart_hex(TOTAL_MEM_SIZE);
    uart_puts(" END="); uart_hex(PHY_MEM_START + TOTAL_MEM_SIZE);
    uart_puts("\n");

    // ======================================================
    // 2. FIND SAFE START FOR STARTUP ALLOCATOR
    // ======================================================
    // Start with the end of the kernel image
    unsigned long safe_start_pa = virt_to_phys((unsigned long)_end);

    // Check DTB location and size
    uint32_t dtb_size = 0;
    unsigned long dtb_pa = 0;
    if (dtb) {
        dtb_pa = virt_to_phys((unsigned long)dtb);
        dtb_size = fdt_totalsize(dtb);
        if (dtb_pa + dtb_size > safe_start_pa) safe_start_pa = dtb_pa + dtb_size;
    }

    // Check Initrd location
    uint64_t initrd_start = 0, initrd_end = 0;
    if (dtb) {
        int chosen = fdt_path_offset(dtb, "/chosen");
        if (chosen >= 0) {
            int ls, le;
            const uint32_t *ps = fdt_getprop(dtb, chosen, "linux,initrd-start", &ls);
            const uint32_t *pe = fdt_getprop(dtb, chosen, "linux,initrd-end", &le);
            if (ps && pe) {
                initrd_start = (ls == 8) ? (((uint64_t)bswap32(ps[0]) << 32) | bswap32(ps[1])) : bswap32(*ps);
                initrd_end   = (le == 8) ? (((uint64_t)bswap32(pe[0]) << 32) | bswap32(pe[1])) : bswap32(*pe);
                if (initrd_end > safe_start_pa) safe_start_pa = initrd_end;
            }
        }
    }

    // ======================================================
    // 3. INITIALIZE ALLOCATOR & ALLOCATE mem_map
    // ======================================================
    bootmem_init(safe_start_pa);
    unsigned long mem_map_size = total_pages * sizeof(struct page);
    mem_map = (struct page *)bootmem_alloc(mem_map_size);

    // Initialize Page Descriptors as FREE
    for (unsigned long i = 0; i < total_pages; i++) {
        list_init(&mem_map[i].list);
        mem_map[i].status = PAGE_FREE; 
        mem_map[i].order = 0;
        mem_map[i].cache_index = CACHE_NONE; 
        mem_map[i].ref_count = 0;
    }

    // Buddy and Slab setup...
    for (int i = 0; i <= MAX_ORDER; i++) { list_init(&free_areas[i].free_list); free_areas[i].nr_free = 0; }
    for (int i = 0; i < CACHE_NUM; i++) { list_init(&caches[i].free_list); caches[i].chunk_size = cache_sizes[i]; }

    // ======================================================
    // 4. PERFORM FORMAL RESERVATIONS
    // ======================================================
    uart_puts("--- Performing Memory Reservations ---\n");

    // A. Kernel
    memory_reserve(virt_to_phys((unsigned long)_start), virt_to_phys((unsigned long)_end) - virt_to_phys((unsigned long)_start));

    // B. DTB
    if (dtb) memory_reserve(dtb_pa, dtb_size);

    // C. Initrd
    if (initrd_end > initrd_start) memory_reserve(initrd_start, initrd_end - initrd_start);

    // D. mem_map ITSELF (The Frame Array)
    memory_reserve(virt_to_phys((unsigned long)mem_map), mem_map_size);

    // E. /memreserve/ Block (Firmware/OpenSBI)
    if (dtb) {
        struct fdt_header *h = (struct fdt_header *)dtb;
        struct fdt_reserve_entry *e = (struct fdt_reserve_entry *)((void *)dtb + bswap32(h->off_mem_rsvmap));
        while (e->address != 0 || e->size != 0) {
            memory_reserve(bswap64(e->address), bswap64(e->size));
            e++;
        }
    }

    // F. Frame Buffer (Calculated from parsed TOTAL_MEM_SIZE)
    {
        // 1920 * 1080 * 4 is ~8.3MB, so we reserve 9MB to be safe
        unsigned long fb_size = 0x00900000; 
        unsigned long fb_start_pa = 0x7F700000; 
        
        uart_puts("[Reserve] Frame Buffer (Aligned to 0x7F700000)...\n");
        memory_reserve(fb_start_pa, fb_size);
    }

    // ======================================================
    // 5. HANDOVER
    // ======================================================
    uart_puts("Handing over to Buddy System...\n");
    for (unsigned long i = 0; i < total_pages; i++) {
        if (mem_map[i].status == PAGE_FREE) {
            mem_map[i].status = PAGE_ALLOCATED; 
            free_pages(&mem_map[i], 0);
        }
    }
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
            if (buddy_verbose) {
                printk("[-] Remove page %d from order %d\n",
                       page_to_pfn(page), current_order);
            }
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
                    printk("[+] Add page %d to order %d. Range: [%d, %d]\n",
                           buddy_pfn, current_order,
                           buddy_pfn, buddy_pfn + (1 << current_order) - 1);
                }
            }
            page->status = PAGE_ALLOCATED;
            page->order = order;
            page->cache_index = CACHE_NONE; 
            if (buddy_verbose) {
                unsigned long addr = (unsigned long)page_to_virt(page);
                printk("[Page] Allocate 0x%lx at order %d, page %d\n",
                       addr, order, page_to_pfn(page));
            }
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
    if (buddy_verbose) {
        unsigned long addr = (unsigned long)page_to_virt(page);
        printk("[Page] Free 0x%x at order %d, page %d\n",
               addr, order, pfn);
    }
    while (current_order < MAX_ORDER) {
        unsigned long buddy_pfn = pfn ^ (1UL << current_order);
        if (buddy_pfn >= total_pages) break; 
        struct page *buddy = &mem_map[buddy_pfn];
        if (buddy->status == PAGE_FREE && buddy->order == current_order) {
            if (buddy_verbose) {
                printk("[*] Buddy found! buddy idx: %d for page %d with order %d\n",
                       buddy_pfn, pfn, current_order);
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
    if (buddy_verbose) {
        printk("[+] Add page %d to order %d. Range: [%d, %d]\n",
               pfn, current_order, pfn, pfn + (1 << current_order) - 1);
    }
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
    // if (buddy_verbose)
    //     printk("[Chunk] Allocate 0x%x at chunk size %d\n",
    //            (unsigned long)page_addr, cache->chunk_size);
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
    // if (buddy_verbose)
    //     printk("[Chunk] Free 0x%x at chunk size %d\n",
    //            (unsigned long)ptr, caches[cache_idx].chunk_size);
    struct kmem_cache *cache = &caches[cache_idx];
    struct list_head *node = (struct list_head *)ptr;
    list_add(node, &cache->free_list);
}


void inc_page_ref(unsigned long phys_addr) {
    unsigned long pfn = (phys_addr - PHY_MEM_START) >> PAGE_SHIFT;
    if (pfn < total_pages) {
        mem_map[pfn].ref_count++;
    }
}

void dec_page_ref(unsigned long phys_addr) {
    unsigned long pfn = (phys_addr - PHY_MEM_START) >> PAGE_SHIFT;
    if (pfn < total_pages) {
        mem_map[pfn].ref_count--;
        
        // THE MAGIC OF CoW: Only free the memory if NO ONE is using it!
        if (mem_map[pfn].ref_count <= 0) {
            mem_map[pfn].ref_count = 0;
            kfree((void *)phys_to_virt(phys_addr));
        }
    }
}