#ifndef __MINI_CRT_H__
#define __MINI_CRT_H__

#define MAX_PATH 256                            /* max lenght of pathname */
#define NAME_LEN 14                             /* max length of filename */
#define DIR_BUF_SIZE 512                        /* size of dir buf */

/* typedef */
typedef unsigned int uint;

/* malloc.c */
#include "malloc.h"

/* stdio.c */
#include "stdio.h"

/* string.c */
#include "string.h"

/* printf.c */
#include "printf.h"

/* unistd.c */
#include "unistd.h"

/* dirent.c */
#include "dirent.h"

#endif      									/* END __MINI_CRT_H__ */