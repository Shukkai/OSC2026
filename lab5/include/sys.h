#ifndef _SYS_H_
#define _SYS_H_

// =========================================================================
// System Call Numbers
// =========================================================================
#define SYS_GETPID      0
#define SYS_UART_READ   1
#define SYS_UART_WRITE  2
#define SYS_EXEC        3
#define SYS_FORK        4
#define SYS_EXIT        5
#define SYS_STOP        6
#define SYS_DISPLAY     7
#define SYS_USLEEP      8
#define SYS_SIGNAL      9
#define SYS_SIGRETURN   10
#define SYS_KILL        11
#define SYS_MMAP        12
#define SYS_OPEN        13
#define SYS_CLOSE       14
#define SYS_READ        15
#define SYS_WRITE       16
#define SYS_MKDIR       17
#define SYS_MOUNT       18
#define SYS_CHDIR       19
#define SYS_LSEEK       20
#define SYS_IOCTL       21

// =========================================================================
// User API Prototypes (Only for C code, ignored by Assembly)
// =========================================================================
#ifndef __ASSEMBLER__

#include <stdint.h>
#include <stddef.h>

// Type definitions for Signal Handling
typedef void (*sighandler_t)(int);

// 0. Process Control
int getpid(void);
int fork(void);
void exit(int status);
int exec(const char *path);
int stop(long pid);
int kill(long pid);
int usleep(unsigned int usec);

// 1. I/O & UART
long uart_read(char *buf, long count);
long uart_write(const char *buf, long count);

// 2. Signals
sighandler_t signal(int signum, sighandler_t handler);
int sigreturn(void);

// 3. File System (Lab 7/8)
int open(const char *path, int flags);
int close(int fd);
long read(int fd, char *buf, long count);
long write(int fd, const char *buf, long count);
int mkdir(const char *path);
int chdir(const char *path);
int mount(const char *source, const char *target, const char *filesystemtype);
long lseek(int fd, long offset, int whence);
int ioctl(int fd, unsigned long op, void *arg);

// 4. Memory & Display
void *mmap(void *addr, long length, int prot, int flags);
void display(unsigned int *bmp_image, unsigned int width, unsigned int height);

int do_fork();
void do_exit(int status);
#endif // __ASSEMBLER__

#endif // _SYS_H_