#ifndef __MINI_STRING_H__
#define __MINI_STRING_H__

/* string.c */
int strcmp(const char * src, const char * dst);
char * itoa(int n, char * str, int radix);
char * strcpy(char * src, const char * dest);
unsigned strlen(const char * str);

#endif                      /* end of __MINI_STRING_H__ */