#include "minicrt.h"

int open(const char * pathname, int flags, int mode) {
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

int read(int fd, void * buffer, unsigned size) {
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

int write(int fd, const void * buffer, unsigned size) {
    int ret = 0;
    /* syscall __NR_write = 4: sys_write */
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

int close(int fd) {
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

int seek(int fd, int offset, int mode) {
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

int fstat(int fd, struct stat * buf) {
    int ret;
    /* syscall __NR_fstat = 28: sys_fstat */
    asm("movl $28, %%eax    \n\t"
        "movl %1, %ebx      \n\t"
        "movl %2, %ecx      \n\t"
        "int $0x80          \n\t"
        "movl %%eax, %0     \n\t"
        : "=m" (ret)
        : "m" (fd), "m" (buf));
    return ret;
}

int chdir(const char * filename) {
    int ret;
    /* syscall __NR_chdir = 12: sys_chdir */
    asm("movl $12, %%eax    \n\t"
        "movl %1, %%ebx     \n\t"
        "int $0x80          \n\t"
        "movl %%eax, %0     \n\t"
        : "=m" (ret)
        : "m" (filename));
    return ret;
}

char * getcwd(char * buf, uint size) {
    if (!buf || size == 0) {
        return NULL;
    }

    char temp[MAX_PATH_LEN];
    temp[size - 1] = '\0';
    char * ptr = temp + size - 1;

    int fd = open(".", OREAD, ORDONLY);
    if (fd < 0) {
        return NULL;
    }

    struct stat current_stat, parent_stat;
    if (fstat(fd, &current_stat) < 0) {
        close(fd);
        return NULL;
    }

    while (1) {
        int parent_fd = open("..", O_RDONLY);
        if (parent_fd < 0) {
            close(fd);
            return NULL;
        }
    }
}