#include "minicrt.h"

#ifndef FILE
#define FILE int
#endif

int mini_crt_io_init() {
    return 1;
}

static int open(const char *pathname, int flags, int mode) {
    int fd = 0;
    asm(
        "movl $5, %%eax;"
        "movl %1, %%ebx;"
        "movl %2, %%ecx;"
        "movl %3, %%edx;"
        "int $0x80;"
        "movl %%eax, %0;"
        : "=m"(fd)
        : "m"(pathname), "m"(flags), "m"(mode));
}

static int read(int fd, void * buffer, unsigned size) {
    int ret = 0;
    asm(
        "movl $3, %%eax;"
        "movl %1, %%ebx;"
        "movl %2, %%ecx;"
        "movl %3, %%edx;"
        "int $0x80;"
        "movl %%eax, %0;"
        : "=m"(ret)
        : "m"(fd), "m"(buffer), "m"(size));
    return ret;
}

static int write(int fd, const void * buffer, unsigned size) {
    int ret = 0;
    asm(
        "movl $4, %%eax;"
        "movl %1, %%ebx;"
        "movl %2, %%ecx;"
        "movl %3, %%edx;"
        "int $0x80;"
        "movl %%eax, %0;"
        : "=m"(ret)
        : "m"(fd), "m"(buffer), "m"(size));
    return ret;
}

static int close(int fd) {
    int ret = 0;
    asm(
        "movl $6, %%eax;"
        "movl %1, %%ebx;"
        "int $0x80;"
        "movl %%eax, %0;"
        : "=m"(ret)
        : "m"(fd));
    return ret;
}

static int seek(int fd, int offset, int mode) {
    int ret = 0;
    asm(
        "movl $19, %%eax;"
        "movl %1, %%ebx;"
        "movl %2, %%ecx;"
        "movl %3, %%edx;"
        "int $0x80;"
        "movl %%eax, %0;"
        : "=m"(ret)
        : "m"(fd), "m"(offset), "m"(mode));
    return ret;
}

FILE * fopen(const char * filename, const char * mode) {
    int fd = -1;
    int flags = 0;
    int access = 00700;     

#define O_RDONLY        00
#define O_WRONLY        01
#define O_RDWR          02
#define O_CREAT         0100
#define O_TRUNC         01000
#define O_APPEND        02000

    if (strcmp(mode, "w") == 0) {
        flags |= O_WRONLY | O_CREAT | O_TRUNC;
    }
    if (strcmp(mode, "w+") == 0) {
        flags |= O_RDWR | O_CREAT | O_TRUNC;
    }
    if (strcmp(mode, "r") == 0) {
        flags |= O_RDONLY;
    }
    if (strcmp(mode, "r+") == 0) {
        flags |= O_RDWR | O_CREAT;
    }
    fd = open(filename, flags, access);
    return (FILE *) fd;
}

int fread(void * buffer, int size, int count, FILE * stream) {
    return read((int) stream, buffer, size * count);
}
int fwrite(const void * buffer, int size, int count, FILE * stream) {
    return write((int) stream, buffer, size * count);
}
int fclose(FILE * fp) {
    return close((int) fp);
}
int fseek(FILE * fp, int offset, int set) {
    return seek((int) fp, offset, set);
}
