/*
 *  linux/fs/read_write.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/segment.h>

// 字符设备读写函数. fs/char_dev.c. 
extern int rw_char(int rw, int dev, char * buf, int count, off_t * pos);
// 读管道操作函数. fs/pipe.c
extern int read_pipe(struct m_inode * inode, char * buf, int count);
// 写管道操作函数. fs/pipe.c
extern int write_pipe(struct m_inode * inode, char * buf, int count);
// 块设备读操作函数. fs/block_dev.c
extern int block_read(int dev, off_t * pos, char * buf, int count);
// 块设备写操作函数. fs/block_dev.c
extern int block_write(int dev, off_t * pos, char * buf, int count);
// 读文件操作函数. fs/file_dev.c
extern int file_read(struct m_inode * inode, struct file * filp, char * buf, int count);
// 写文件操作函数. fs/file_dev.c
extern int file_write(struct m_inode * inode, struct file * filp, char * buf, int count);

// 重定位文件读写指针系统调用. 
// 参数 fd 是文件句柄, offset 是新的文件读写指针偏移值, origin 是偏移的起始位置, 
// 可有三种选择: SEEK_SET(0, 从文件开始处), SEEK_CUR(1, 从当前读写位置), SEEK_END(2, 从文件尾处). 
int sys_lseek(unsigned int fd, off_t offset, int origin) {
	struct file * file;
	int tmp;

	// 首先判断函数提供的参数有效性. 如果文件句柄大于程序最多打开文件数 NR_OPEN(20), 或者该句柄的文件结构指针为空, 
	// 或者对应文件结构的 i 节点字段为空, 或者指定设备文件指针是不可定位的, 则返回出错码并退出. 
	// 如果文件对应 i 节点是管道节点, 则返回出错码退出. 因为管道头尾指针不可随意移动！
	if (fd >= NR_OPEN || !(file = current->filp[fd]) || !(file->f_inode) || !IS_SEEKABLE(MAJOR(file->f_inode->i_dev))) {
		return -EBADF;
	}
	if (file->f_inode->i_pipe) {
		return -ESPIPE;
	}
	// 然后根据设置的定位标志, 分别重新定位文件读写指针. 
	switch (origin) {
		// origin = SEEK_SET, 要求以文件起始处作为原点设置文件读写指针. 
		// 若偏移值小于零, 则出错返回错误码. 否则设置文件读写指针等于 offset. 
		case 0:
			if (offset < 0) {return -EINVAL;}
			file->f_pos = offset;
			break;
		// origin = SEEK_CUR, 要求以文件当前读写指针处作为原点重定位读写指针. 
		// 如果文件当前指针加上偏移值小于 0, 则返回出错码退出. 否则在当前读写指针上加上偏移值. 
		case 1:
			if (file->f_pos + offset < 0) {return -EINVAL;}
			file->f_pos += offset;
			break;
		// origin = SEEK_END, 要求以文件末尾作为原点重定位读写指针. 
		// 此时若文件大小加上偏移值小于零则返回出错码退出. 否则重定位读写指针为文件长度加上偏移值. 
		case 2:
			if ((tmp = file->f_inode->i_size + offset) < 0) {
				return -EINVAL;
			}
			file->f_pos = tmp;
			break;
		// origin 设置无效, 返回出错码退出. 
		default:
			return -EINVAL;
	}
	return file->f_pos;             					// 最后返回重定位后的文件读写指针值. 
}

// 读文件系统调用. 
// 参数 fd 是文件句柄, buf 是缓冲区, count 是欲读字节数. 
int sys_read(unsigned int fd, char * buf, int count) {
	struct file * file;
	struct m_inode * inode;

	// 同样地, 我们首先判断函数参数的有效性. 如果进程文件句柄值大于程序最多打开文件数 NR_OPEN, 
	// 或者需要写入的字节计数小于 0, 或者该句柄的文件结构指针为空, 则返回出错码并退出. 
	// 如果需读取的字节数 count 等于 0, 则返回 0 退出.
	if (fd >= NR_OPEN || count < 0 || !(file = current->filp[fd])) {
		return -EINVAL;
	}
	if (!count) {
		return 0;
	}
	// 然后验证存放数据的缓冲区内存限制. 并取文件的 i 节点. 用于根据该 i 节点的属性, 分别调用相应的读操作函数. 
	// 若是管道操作, 并且是读管道文件模式, 则进行读管道操作, 若成功则返回读取的字节数, 否则返回出错码, 退出. 
	// 如果是字符型文件, 则进行读字符设备操作, 并返回读取的字符数. 如果是块设备文件, 则执行块设备读操作, 并返回读取的字节数. 
	verify_area(buf, count);
	inode = file->f_inode;
	// 管道文件的读操作.
	if (inode->i_pipe) {
		return (file->f_mode & 1) ? read_pipe(inode, buf, count) : -EIO;
	}
	// 字符设备的读操作.
	if (S_ISCHR(inode->i_mode)) {
		return rw_char(READ, inode->i_zone[0], buf, count, &file->f_pos);
	}
	// 块设备的读操作.
	if (S_ISBLK(inode->i_mode)) {
		return block_read(inode->i_zone[0], &file->f_pos, buf, count);
	}
	// 如果是目录文件或者是常规文件, 
	// 则首先验证读取字节数 count 的有效性并进行调整(若读取字节数加上文件当前读写指针值大于文件长度, 
	// 则重新设置读取字节数为文件长度 - 当前读写指针值, 若读取数等于 0, 则返回 0 退出), 
	// 然后执行文件读操作, 返回读取的字节数并退出. 
	if (S_ISDIR(inode->i_mode) || S_ISREG(inode->i_mode)) {
		if (count + file->f_pos > inode->i_size) {
			count = inode->i_size - file->f_pos;
		}
		if (count <= 0) {
			return 0;
		}
		return file_read(inode, file, buf, count);
	}
	// 执行到这里, 说明我们无法判断文件的属性. 则打印节点文件的属性, 并返回出错码退出. 
	printk("(Read)inode->i_mode=%06o\n\r", inode->i_mode);
	return -EINVAL;
}

// 写文件系统调用.
// 参数 fd 是文件句柄, buf 是用户缓冲区, count 是欲写字节数.
int sys_write(unsigned int fd, char * buf, int count) {
	struct file * file;
	struct m_inode * inode;

	// 同样地, 我们首先判断函数参数的有效性. 如果进程文件句柄值大于程序最多打开文件数 NR_OPEN, 
	// 或者需要写入的字节计数小于 0, 或者该句柄的文件结构指针为空,
	// 则返回出错码并退出. 如果需读取的字节数 count 等于 0, 则返回 0 退出.
	if (fd >= NR_OPEN || count < 0 || !(file = current->filp[fd])) {
		return -EINVAL;
	}
	if (!count) {
		return 0;
	}
	// 然后验证存放数据的缓冲区内存限制. 并取文件的 i 节点. 根据该 i 节点的属性, 分别调用相应的写操作函数. 
	// 若是管道文件, 并且是写管道文件模式, 则进行写管道操作, 若成功则返回写入的字节数, 否则返回出错码退出. 
	// 如果是字符设备文件, 则进行写字符设备操作, 返回写入的字符数退出. 
	// 如果是块设备文件, 则进行块设备写操作, 并返回写入的字节数退出. 
	// 若是常规文件, 则执行文件写操作, 并返回写入的字节数, 退出.
	inode = file->f_inode;
	// 管道的写操作.
	if (inode->i_pipe) {
		return (file->f_mode & 2) ? write_pipe(inode, buf, count) : -EIO;
	}
	// 字符设备的写操作.
	if (S_ISCHR(inode->i_mode)) {
		return rw_char(WRITE, inode->i_zone[0], buf, count, &file->f_pos);
	}
	// 块设备的写操作.
	if (S_ISBLK(inode->i_mode)) {
		return block_write(inode->i_zone[0], &file->f_pos, buf, count);
	}
	// 文件的写操作.
	if (S_ISREG(inode->i_mode)) {
		return file_write(inode, file, buf, count);
	}
	// 执行到这里, 说明我们无法判断文件的属性. 则打印节点文件属性, 并返回出错码退出.
	printk("(Write)inode->i_mode=%06o\n\r", inode->i_mode);
	return -EINVAL;
}
