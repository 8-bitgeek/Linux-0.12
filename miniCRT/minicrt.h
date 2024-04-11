void mini_crt_entry(void);

// minicrt.c
int mini_crt_io_init();

// malloc.c
int mini_crt_heap_init();
void free(void * ptr);
void * malloc(unsigned size);