#ifndef __MINI_DIRENT_H__
#define __MINI_DIRENT_H__

/* dirent.c */
struct dirent {
    unsigned short inode;
    char name[NAME_LEN];
};

typedef struct {
    FILE fd;
    unsigned int buf_pos;
    unsigned int buf_size;
    char buf[DIR_BUF_SIZE];
} DIR;

DIR * opendir(const char * name);
struct dirent * readdir(DIR * dir);

#endif                          /* end of __MINI_DIRENT_H__ */