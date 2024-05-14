#include "minicrt.h"

#ifndef NULL
#define NULL 0
#endif

typedef struct _heap_header {
    enum {
        HEAP_BLOCK_FREE = 0xABAB,
        HEAP_BLOCK_USED = 0xCDCD,
    } type;
    unsigned size;                  
    struct _heap_header * next;
    struct _heap_header * prev;
} heap_header;

static heap_header * list_head = NULL;

#define ADDR_ADD(a, o) (((char *) (a)) + o)
#define HEADER_SIZE (sizeof(heap_header))

void free(void * ptr) {
    heap_header * header = (heap_header *) ADDR_ADD(ptr, -HEADER_SIZE);

    if (header->type != HEAP_BLOCK_USED) { 
        return;
    }

    header->type = HEAP_BLOCK_FREE;
    if (header->prev != NULL && header->prev->type == HEAP_BLOCK_FREE) { 
        
        header->prev->next = header->next;
        if (header->next != NULL) {
            header->next->prev = header->prev;
        }
        header->prev->size += header->size;
        header = header->prev;
    }

    if (header->next != NULL && header->next->type == HEAP_BLOCK_FREE) { 
        
        header->size += header->next->size;
        header->next = header->next->next;
    }
    
}

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
            return header;
        }
        if (header->size > size + HEADER_SIZE * 2) {
            
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
    
    return NULL;
}

static int brk(void * end_data_segment) {
    int ret = 0;
    
    
    
    
    asm volatile (
        "movl $45, %%eax    \n\t"
        "movl %1, %%ebx     \n\t"
        "int $0x80          \n\t"
        "movl %%eax, %0     \n\t"
        : "=r"(ret)                             
        : "m"(end_data_segment));               
    return ret;
}

int mini_crt_heap_init() {
    heap_header * header = 0;
    
    unsigned heap_size = 1024 * 1024 * 32;

    char * base = (char *) brk(0);                     
    char * end = ADDR_ADD(base, heap_size);
    end = (char *) brk(end);
    if (!end) {
        return 0;
    }
    header = (heap_header *) base;              
    header->size = heap_size;
    header->type = HEAP_BLOCK_FREE;
    header->next = NULL;
    header->prev = NULL;

    list_head = header;
    return 1;
}
