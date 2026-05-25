#pragma once

#include<stdint.h>

/* Define the struct with a tag so 'struct sbiret' is valid */
struct sbiret {
    long error;
    long value;
};

/* SBI Extension IDs */
#define SBI_EXT_BASE            0x10
#define SBI_EXT_SRST            0x53525354

/* SBI Base Function IDs */
#define SBI_FID_GET_SPEC_VERSION 0
#define SBI_FID_GET_IMPL_ID      1
#define SBI_FID_GET_IMPL_VERSION 2
#define SBI_FID_PROBE_EXTENSION  3

/* SBI Reset Function IDs */
#define SBI_FID_SYSTEM_RESET     0

/* Reset Types (for sbi_system_reset) */
#define SBI_RESET_TYPE_SHUTDOWN    0
#define SBI_RESET_TYPE_COLD_REBOOT 1
#define SBI_RESET_TYPE_WARM_REBOOT 2

/* Reset Reasons */
#define SBI_RESET_REASON_NONE      0

/* Function Prototypes */
struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0,
                        unsigned long arg1, unsigned long arg2,
                        unsigned long arg3, unsigned long arg4,
                        unsigned long arg5);

struct sbiret sbi_get_spec_version(void);
struct sbiret sbi_get_impl_id(void);
struct sbiret sbi_get_impl_version(void);
struct sbiret sbi_probe_extension(long extid);
struct sbiret sbi_system_reset(unsigned int type, unsigned int reason);
void sbi_set_timer(uint64_t stime_value);