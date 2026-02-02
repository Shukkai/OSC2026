#pragma once
#include <stdint.h>
#include <stddef.h>
/* Byte Swapping */
uint32_t bswap32(uint32_t x);
uint64_t bswap64(uint64_t x);

/* Hex String Parsing (for CPIO) */
unsigned long parse_hex8(const char *s);

/* Memory Alignment */
uintptr_t align_up(uintptr_t ptr, size_t align);

int atoi(char *str);

void print_dtb_info(unsigned long dtb_addr);

uint64_t get_pc();