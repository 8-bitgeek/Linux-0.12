#include "minicrt.h"

#ifndef NULL
#define NULL 0
#endif

typedef struct _heap_header {
    enum {
        HEAP_BLOCK_FREE = 0xABABABAB,
        HEAP_BLOCK_USED = 0xCDCDCDCD,
    } type;
    unsigned size;                  // size contains sizeof(heap_header) and free space.
    struct _heap_header * next;
    struct _heap_header * prev;
} heap_header;

static heap_header * list_head = NULL;

#define ADDR_ADD(a, o) (((char *) (a)) + o)
#define HEADER_SIZE (sizeof(heap_header))

void free(void * ptr) {
    heap_header * header = (heap_header *) ADDR_ADD(ptr, -HEADER_SIZE);

    if (header->type != HEAP_BLOCK_USED) { // if the heap is not used just return.
        return;
    }

    header->type = HEAP_BLOCK_FREE;
    if (header->prev != NULL && header->prev->type == HEAP_BLOCK_FREE) { // if prev is free then merger into it.
        // merge
        header->prev->next = header->next;
        if (header->next != NULL) {
            header->next->prev = header->prev;
        }
        header->prev->size += header->size;
        header = header->prev;
    }

    if (header->next != NULL && header->next->type == HEAP_BLOCK_FREE) { // if next is free then merge it.
        // merge
        header->size += header->next->size;
        header->next->next->prev = header;
        header->next = header->next->next;
    }
    // if no adjoin free space then do nothing but just free current space.
}

void * malloc(unsigned size) {
    heap_header * header;
    if (size == 0) {
        return NULL;
    }

    header = list_head;

    while (header != 0) {
        if (header->type == HEAP_BLOCK_USED) {
            header = header->next;                  // look up unused heap space.
            continue;
        }
        // if heap space just satisfy demand, then return this header.
        if (header->size > size + HEADER_SIZE && header->size <= size + HEADER_SIZE * 2) {
            header->type = HEAP_BLOCK_USED;
            return header;
        }
        if (header->size > size + HEADER_SIZE * 2) {
            // split
            heap_header * next = (heap_header *) ADDR_ADD(header, size + HEADER_SIZE);
            next->prev = header;
            next->next = header->next;
            next->type = HEAP_BLOCK_FREE;
            next->size = header->size - (size - HEADER_SIZE);
            header->next = next;
            header->size = size + HEADER_SIZE;
            header->type = HEAP_BLOCK_USED;
            return ADDR_ADD(header, HEADER_SIZE);
        }
    }
    // if no heap space then return NULL.
    return NULL;
}

static int brk(void * end_data_segment) {
    int ret = 0;
    // brk system call number: 45.
    // include/unistd.h #define __NR_brk 45
    // 汇编指令每条以 "" 为单位, 多条指令之间用 ; 号, '\n\t' 或者换行来进行分割.
    // kernel/sys.c  sys_brk();
    asm volatile (
        "movl $45, %%eax    \n\t"
        "movl %1, %%ebx     \n\t"
        "int $0x80          \n\t"
        "movl %%eax, %0     \n\t"
        : "=r"(ret)                             // '=' 表示操作数是只写的, 'r' 表示通用寄存器.
        : "m"(end_data_segment));               // 'm' 表示内存变量.
    return ret;
}

int mini_crt_heap_init() {
    void * base = NULL;
    heap_header * header = NULL;
    // 32MB heap size.
    unsigned heap_size = 1024 * 1024 * 32;

    // get current task's brk as base: pass 0 to sys_brk then return current brk.
    base = (void *) brk(0);                     // default task.brk = code_size + data_size + bss_size.
    void * end = ADDR_ADD(base, heap_size);
    end = (void *) brk(end);
    if (!end) {
        return 0;
    }
    header = (heap_header *) base;              // header at end of bss.
    header->size = heap_size;
    header->type = HEAP_BLOCK_FREE;
    header->next = NULL;
    header->prev = NULL;

    list_head = header;
    return 1;
}
