#include "minicrt.h"

int mini_crt_io_init() {
    return 1;
}

static int open(const char * pathname, int flags, int mode) {
    int fd = 0;
    /* syscall __NR_open = 5: sys_open() */
    asm("movl $5, %%eax     \n\t"
        "movl %1, %%ebx     \n\t"
        "movl %2, %%ecx     \n\t"
        "movl %3, %%edx     \n\t"
        "int $0x80          \n\t"
        "movl %%eax, %0     \n\t"
        : "=m" (fd)
        : "m" (pathname), "m" (flags), "m" (mode));
    return fd;
}

static int read(int fd, void * buffer, unsigned size) {
    int ret = 0;
    /* syscall __NR_read = 3: sys_read() */
    asm("movl $3, %%eax     \n\t"
        "movl %1, %%ebx     \n\t"
        "movl %2, %%ecx     \n\t"
        "movl %3, %%edx     \n\t"
        "int $0x80          \n\t"
        "movl %%eax, %0     \n\t"
        : "=m" (ret)
        : "m" (fd), "m" (buffer), "m" (size));
    return ret;
}

static int write(int fd, const void * buffer, unsigned size) {
    int ret = 0;
    asm("movl $4, %%eax     \n\t"
        "movl %1, %%ebx     \n\t"
        "movl %2, %%ecx     \n\t"
        "movl %3, %%edx     \n\t"
        "int $0x80          \n\t"
        "movl %%eax, %0     \n\t"
        : "=m" (ret)
        : "m" (fd), "m" (buffer), "m" (size));
    return ret;
}

static int close(int fd) {
    int ret = 0;
    /* syscall __NR_close = 6: sys_close */
    asm("movl $6, %%eax     \n\t"
        "movl %1, %%ebx     \n\t"
        "int $0x80          \n\t"
        "movl %%eax, %0     \n\t"
        : "=m" (ret)
        : "m" (fd));
    return ret;
}

static int seek(int fd, int offset, int mode) {
    int ret = 0;
    /* syscall __NR_seek = 19: sys_seek */
    asm("movl $19, %%eax    \n\t"
        "movl %1, %%ebx     \n\t"
        "movl %2, %%ecx     \n\t"
        "movl %3, %%edx     \n\t"
        "int $0x80          \n\t"
        "movl %%eax, %0     \n\t"
        : "=m" (ret)
        : "m" (fd), "m" (offset), "m" (mode));
    return ret;
}

