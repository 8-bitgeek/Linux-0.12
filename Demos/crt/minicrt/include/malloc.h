#ifndef __MINI_MALLOC_H__
#define __MINI_MALLOC_H__

/* malloc.c */
#ifndef NULL
#define NULL    (0)
#endif

void free(void * ptr);
void * malloc(unsigned size);
int mini_crt_heap_init();

#endif                      /* end of __MINI_MALLOC_H__ */