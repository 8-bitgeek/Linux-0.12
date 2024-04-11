#include "minicrt.h"

#ifndef NULL
#define NULL 0
#endif

typedef struct _heap_header {
    enum {
        HEAP_BLOCK_FREE = 0xABABABAB,
        HEAP_BLOCK_USED = 0xCDCDCDCD,
    } type;
    unsigned size;
    struct _heap_header * next;
    struct _heap_header * prev;
} heap_header;

static heap_header * list_head = NULL;

#define ADDR_ADD(a, o) (((char *) (a)) + o)
#define HEADER_SIZE (sizeof(heap_header))

void * malloc(unsigned size) {
    heap_header * header;
    if (size == 0) {
        return NULL;
    }

    header = list_head;

    while (header != 0) {
        if (header->type == HEAP_BLOCK_USED) {
            header = header->next;
            continue;
        }

        if (header->size > size + HEADER_SIZE && header->size <= size + HEADER_SIZE * 2) {
            header->type = HEAP_BLOCK_USED;
        }
        if (header->size > size + HEADER_SIZE * 2) {
            // split
        }
    }
}

#ifndef WIN32
static int brk(void * end_data_segment) {
    int ret = 0;
    // brk system call number: 45.
    // include/unistd.h #define __NR_brk 45
    // 汇编指令每条以 "" 为单位, 多条指令之间用 ; 号, '\n\t' 或者换行来进行分割.
    asm volatile ("movl $45, %%eax    \n\t"
        "movl %1, %%ebx     \n\t"
        "int $0x80          \n\t"
        "movl %%eax, %0     \n\t"
        : "=r"(ret)                             // '=' 表示操作数是只写的, 'r' 表示通用寄存器.
        : "m"(end_data_segment));               // 'm' 表示内存变量.
    return ret;
}
#else           // If define WIN32.
#include <Windows.h>
#endif

int mini_crt_heap_init() {
    void * base = NULL;
    heap_header * header = NULL;
    // 32MB heap size.
    unsigned heap_size = 1024 * 1024 * 32;

#ifdef WIN32
#else 
    base = (void *) brk(0);
    void * end = ADDR_ADD(base, heap_size);
    end = (void *) brk(end);
    if (!end) {
        return 0;
    }
#endif
    header = (heap_header *) base;
    header->size = heap_size;
    header->type = HEAP_BLOCK_FREE;
    header->next = NULL;
    header->prev = NULL;

    list_head = header;
    return 1;
}