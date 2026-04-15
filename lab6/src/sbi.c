#include "sbi.h"
#include "stdint.h"

struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0,
                        unsigned long arg1, unsigned long arg2,
                        unsigned long arg3, unsigned long arg4,
                        unsigned long arg5)
{
    struct sbiret ret;

    register unsigned long a0 asm("a0") = (unsigned long)arg0;
    register unsigned long a1 asm("a1") = (unsigned long)arg1;
    register unsigned long a2 asm("a2") = (unsigned long)arg2;
    register unsigned long a3 asm("a3") = (unsigned long)arg3;
    register unsigned long a4 asm("a4") = (unsigned long)arg4;
    register unsigned long a5 asm("a5") = (unsigned long)arg5;
    register unsigned long a6 asm("a6") = (unsigned long)fid;
    register unsigned long a7 asm("a7") = (unsigned long)ext;

    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");

    ret.error = a0;
    ret.value = a1;

    return ret;
}

struct sbiret sbi_get_spec_version(void)
{
    return sbi_ecall(SBI_EXT_BASE, SBI_FID_GET_SPEC_VERSION, 0, 0, 0, 0, 0, 0);
}

struct sbiret sbi_get_impl_id(void)
{
    return sbi_ecall(SBI_EXT_BASE, SBI_FID_GET_IMPL_ID, 0, 0, 0, 0, 0, 0);
}

struct sbiret sbi_get_impl_version(void)
{
    return sbi_ecall(SBI_EXT_BASE, SBI_FID_GET_IMPL_VERSION, 0, 0, 0, 0, 0, 0);
}

struct sbiret sbi_probe_extension(long extid)
{
    return sbi_ecall(SBI_EXT_BASE, SBI_FID_PROBE_EXTENSION, extid, 0, 0, 0, 0, 0);
}

struct sbiret sbi_system_reset(unsigned int type, unsigned int reason)
{
    struct sbiret ret;
    
    /* 1. Try the modern System Reset Extension (SRST) */
    ret = sbi_ecall(SBI_EXT_SRST, SBI_FID_SYSTEM_RESET, type, reason, 0, 0, 0, 0);
    
    /* 2. If valid (even if failed), return it. 
     * If Not Supported (-2), try Fallback. */
    if (ret.error != -2) {
        return ret;
    }

    /* 3. Fallback: Legacy Extension 0x08 (Shutdown) 
     * Note: This does not reboot; it halts the system. 
     * Users must press the physical reset button. 
     */
    return sbi_ecall(0x08, 0, 0, 0, 0, 0, 0, 0);
}


void sbi_set_timer(uint64_t stime_value)
{
    sbi_ecall(0x00, 0, stime_value, 0, 0, 0, 0, 0);
}