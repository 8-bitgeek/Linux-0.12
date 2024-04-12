void mini_crt_entry(void);

typedef int FILE;
#define EOF (-1)

#ifdef WIN32
#else
#define stdin       ((FILE *)0)
#define stdout      ((FILE *)1)
#define stderr      ((FILE *)2)
#endif

// minicrt.c
int mini_crt_io_init();

// malloc.c
int mini_crt_heap_init();
void free(void * ptr);
void * malloc(unsigned size);