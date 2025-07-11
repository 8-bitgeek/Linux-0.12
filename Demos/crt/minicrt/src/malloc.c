#include "minicrt.h"

typedef struct _heap_header {
    enum {
        HEAP_BLOCK_FREE = 0XABAB,       /* magic number of free block */
        HEAP_BLOCK_USED = 0XCDCD        /* magic number of used block */
    } type;                             /* block type: FREE/USED */

    unsigned size;                      /* block size including header */
    struct _heap_header * next;
    struct _heap_header * prev;
} heap_header;

#define ADDR_ADD(a, o)  (((char *) a) + o)
#define HEADER_SIZE     (sizeof(heap_header))
#define HEAP_SIZE       (1024 * 1024 * 32)                          /* 32MB heap size */

static heap_header * head_list = NULL;

void free(void * ptr) {
    heap_header * header = (heap_header *) ADDR_ADD(ptr, -HEADER_SIZE);

    if (header->type == HEAP_BLOCK_FREE) {
        return;
    }
    header->type = HEAP_BLOCK_FREE;

    /* if prev is free then merge it to prev */
    if (header->prev != NULL && header->prev->type == HEAP_BLOCK_FREE) {
        header->prev->next = header->next;
        if (header->next != NULL) {
            header->next->prev = header->prev;
        }
        header->prev->size += header->size;
        header = header->prev;
    }
    /* if next is free, merge it to header */
    if (header->next != NULL && header->next->type == HEAP_BLOCK_FREE) {
        header->size += header->next->size;
        header->next = header->next->next;
    }
}

void * malloc(unsigned size) {
    heap_header * header;

    if (size == 0 || size >= (HEAP_SIZE - HEADER_SIZE)) {
        return NULL;
    }

    header = head_list;
    while (header != 0) {
        /* if not free, skip it */
        if (header->type == HEAP_BLOCK_USED) {
            header = header->next;
            continue;
        }

        /* just enough to alloc */
        if (header->size > (size + HEADER_SIZE) && header->size <= (size + HEADER_SIZE * 2)) {
            header->type = HEAP_BLOCK_USED;
            return ADDR_ADD(header, HEADER_SIZE);
        }

        /* if too large then we should split it */
        if (header->size > size + (HEADER_SIZE * 2)) {
            /* split it: |<--header----- used -----)|<--next----- free ----- | */
            heap_header * next = (heap_header *) ADDR_ADD(header, size + HEADER_SIZE);  /* new smaller block */
            next->type = HEAP_BLOCK_FREE;
            /* header->size = 128b - (10b + 2b) == 126b == next->size */
            next->size = header->size - (HEADER_SIZE + size);                       /* TODO: is this right? */
            next->prev = header;
            next->next = header->next;
            header->next = next;
            header->size = HEADER_SIZE + size;
            header->type = HEAP_BLOCK_USED;
            return ADDR_ADD(header, HEADER_SIZE);                                   /* now we find the block */
        }
        header = header->next;
    }

    return NULL;                                                                    /* no block can be used */
}

static int brk(void * end_data_segment) {
    int ret = 0;

    /* syscall __NR_brk = 45: sys_brk() */
    asm("movl $45, %%eax    \n\t"
        "movl %1, %%ebx     \n\t"
        "int $0x80          \n\t"
        "movl %%eax, %0     \n\t"
        : "=r" (ret)
        : "m" (end_data_segment));
    return ret;
}

int mini_crt_heap_init() {
    heap_header * header = NULL;

    char * base = (char *) brk(0);                              /* get heap space start addr */
    char * end = ADDR_ADD(base, HEAP_SIZE);                     /* heap space end addr */
    end = (char *) brk(end);                                    /* set end addr of heap: make current task's brk = end by system call */
    if (!end) {
        return 0;
    }

    header = (heap_header *) base;                              /* points to start of heap space */

    header->size = HEAP_SIZE;
    header->type = HEAP_BLOCK_FREE;
    header->next = NULL;
    header->prev = NULL;
    
    head_list = header;
    return 1;
}