#ifndef __MINI_STDIO_H__
#define __MINI_STDIO_H__

/* stdio.c */
#define O_RDONLY    00
#define O_WRONLY    01
#define O_RDWR      02
#define O_CREAT     0100
#define O_TRUNC     01000
#define O_APPEND    02000
typedef int FILE;
#define EOF     -1

#define stdin   ((FILE *) 0)
#define stdout  ((FILE *) 1)
#define stderr  ((FILE *) 2)

int mini_crt_io_init();
FILE * fopen(const char * filename, const char * mode);
int fread(void * buffer, int size, int count, FILE * stream);
int fwrite(const void * buffer, int size, int count, FILE * stream);
int fclose(FILE * fp);
int fseek(FILE * fp, int offset, int set);
int fputc(int c, FILE * stream);
int fputs(const char * str, FILE * stream);
int fgetc(FILE * stream);
char * fgets(char * str, int size, FILE * stream);

#endif                      /* end of __MINI_STDIO_H__ */