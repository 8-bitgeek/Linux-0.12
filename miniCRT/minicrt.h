void mini_crt_entry(void);

typedef int FILE;
#define EOF (-1)

#ifdef WIN32
#else
#define stdin       ((FILE *)0)
#define stdout      ((FILE *)1)
#define stderr      ((FILE *)2)
#endif

#ifndef WIN32
#define va_list             char *
#define va_start(ap, arg)   (ap = (va_list) &arg + sizeof(arg))
#define va_arg(ap, t)       (*(t*)(ap += sizeof(t)) - sizeof(t))
#define va_end(ap)          (ap = (va_list)0)
#else
#include <Windows.h>
#endif

// minicrt.c

// malloc.c
int mini_crt_heap_init();
void free(void * ptr);
void * malloc(unsigned size);

// printf.c
int fputc(int c, FILE * stream);
int fputs(const char * str, FILE * stream);
int vfprintf(FILE * stream, const char * format, va_list arglist);
int printf(const char * format, ...);
int fprintf(FILE * stream, const char * format, ...);

// string.c
char * itoa(int n, char * str, int radix);
int strcmp(const char * src, const char * dst);
char * strcpy(char * dest, const char * src);
unsigned strlen(const char * str);

// stdio.c
FILE * fopen(const char * filename, const char * mode);
int fread(void * buffer, int size, int count, FILE * stream);
int fwrite(const void * buffer, int size, int count, FILE * stream);
int fclose(FILE * fp);
int fseek(FILE * fp, int offset, int set);
int mini_crt_io_init();