/* include/fdt.h */
#pragma once
#include <stdint.h>

#define FDT_MAGIC       0xd00dfeed
#define FDT_BEGIN_NODE  0x00000001
#define FDT_END_NODE    0x00000002
#define FDT_PROP        0x00000003
#define FDT_NOP         0x00000004
#define FDT_END         0x00000009

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap; // <--- Offset to Memory Reservation Block
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

// --- For Memory Reservation Block (Spec v0.4, Sec 5.3) ---
struct fdt_reserve_entry {
    uint64_t address;
    uint64_t size;
};

int fdt_path_offset(const void *fdt, const char *path);
const void *fdt_getprop(const void *fdt, int nodeoffset, const char *name, int *lenp);
int fdt_next_node(const void *fdt, int offset, int *depth);
uint64_t fdt_get_uart_base(const void *fdt);
uint32_t fdt_totalsize(const void *fdt);