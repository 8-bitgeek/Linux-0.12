#ifndef __MINI_CRT_H__
#define __MINI_CRT_H__

#define MAX_PATH 256                            /* max lenght of pathname */
#define NAME_LEN 14                             /* max length of filename */
#define DIR_BUF_SIZE 512                        /* size of dir buf */

/* typedef */
typedef unsigned int uint;

/* malloc.c */
#ifndef NULL
#define NULL    (0)
#endif

void free(void * ptr);
void * malloc(unsigned size);
int mini_crt_heap_init();

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


/* string.c */
int strcmp(const char * src, const char * dst);
char * itoa(int n, char * str, int radix);
char * strcpy(char * src, const char * dest);
unsigned strlen(const char * str);

/* printf.c */
int printf(const char * format, ...);
int fprintf(FILE * stream, const char * format, ...);

/* unistd.c */
int open(const char * pathname, int flags, int mode);
int read(int fd, void * buffer, unsigned size);
int write(int fd, const void * buffer, unsigned size);
int close(int fd);
int seek(int fd, int offset, int mode);
int chdir(const char * filename);
char * getcwd(char * buf, int size);
typedef int pid_t;				/* 用于进程号和进程组号 */
typedef unsigned short uid_t;	/* 用于用户号(用户标识号) */
typedef unsigned short gid_t;	/* 用于组号 */
typedef unsigned short dev_t;	/* 用于设备号 */
typedef unsigned short ino_t;	/* 用于文件序列号 */
typedef unsigned short mode_t;	/* 用于某些文件属性 */
typedef unsigned short umode_t;
typedef unsigned char nlink_t;	/* 用于链接计数 */
typedef int daddr_t;
typedef long off_t;             /* 用于文件长度(大小). */
typedef unsigned char u_char;   /* 无符号字符类型.  */
typedef unsigned short ushort;  /* 无符号短整型类型.  */
typedef long time_t;			/* 从 ＧMT1970 年 1 月 1 日午夜 0 时起开始计的时间(秒). */
struct stat {
	dev_t	st_dev;		/* 含有文件的设备号 */
	ino_t	st_ino;		/* 文件 inode 号 */
	unsigned short st_mode;	/* 文件类型和属性(见下面). */
	nlink_t	st_nlink;	/* 指定文件的连接数. */
	uid_t	st_uid;		/* 文件的用户(标识)号. */
	gid_t	st_gid;		/* 文件的组号. */
	dev_t	st_rdev;	/* 设备号(如果文件是特殊的字符文件或块文件). */
	off_t	st_size;	/* 文件大小(字节数)(如果文件是常规文件). */
	time_t	st_atime;	/* 上次(最后)访问时间. */
	time_t	st_mtime;	/* 最后修改时间. */
	time_t	st_ctime;	/* 最后节点修改时间. */
};
int fstat(int fd, struct stat * buf);

/* dirent.c */
struct dirent {
    unsigned short inode;
    char name[NAME_LEN];
};

typedef struct {
    FILE fd;
    unsigned long buf_pos;
    unsigned long buf_size;
    char buf[DIR_BUF_SIZE];
} DIR;

DIR * opendir(const char * name);
struct dirent * readdir(DIR * dir);

#endif      /* END __MINI_CRT_H__ */