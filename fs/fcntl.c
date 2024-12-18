/*
 *  linux/fs/fcntl.c
 *
 *  (C) 1991  Linus Torvalds
 */

//#include <string.h>
#include <errno.h>
#include <linux/sched.h>
//#include <linux/kernel.h>
//#include <asm/segment.h>

#include <fcntl.h>
// #include <sys/stat.h>

extern int sys_close(int fd);

// 复制文件句柄(文件描述符).
// 参数: fd - 要复制的文件句柄, arg - 指定新文件句柄的最小数值.
// 返回新文件句柄或出错码.
static int dupfd(unsigned int fd, unsigned int arg)
{
	// 首先检查函数参数的有效性. 
	// 如果文件句柄值大于一个程序最多打开文件数 NR_OPEN, 或者要复制的句柄文件结构不存在, 则返回出错码并退出. 
	// 如果指定的新句柄值 arg 大于最多打开文件数, 也返回出错码并退出. 
	// 文件句柄就是文件在当前进程的文件列表中的索引号.
	if (fd >= NR_OPEN || !current->filp[fd])
		return -EBADF;
	if (arg >= NR_OPEN)
		return -EINVAL;
	// 然后在当前进程的文件列表中寻找索引号等于或大于 arg 但还没有被使用的文件项. 
	while (arg < NR_OPEN)
		if (current->filp[arg])  							// 如果非空闲项, 则查找下一个.
			arg++;
		else
			break;
	// 如果没有找到空闲项, 则返回出错码.
	if (arg >= NR_OPEN)
		return -EMFILE;
	// 如果找到空闲文件项, 则在执行时关闭标志位图 close_on_exec 中复位(0)新的句柄位. 
	// 即在运行 exec() 类函数时, 不会关闭通过 dup() 创建的句柄. 
	current->close_on_exec &= ~(1 << arg); 					// 将 arg 对应的位复位, 表示这个句柄在运行 exec() 类函数时不会被关闭.
	// 并令新文件项指针指向原文件句柄 fd 指针指向的文件(在系统文件表 file_table[] 中), 并且将该文件引用数 +1. 最后返回新的文件句柄 arg.
	(current->filp[arg] = current->filp[fd])->f_count++;	// 复制文件指针, 并增加文件引用计数值.
	return arg;
}

// 复制文件句柄系统调用.
// 复制指定文件句柄 oldfd, 新文件句柄值等于 newfd. 如果 newfd 已打开, 则首先关闭之.
// 参数: oldfd -- 原文件句柄; newfd - 新文件句柄.
// 返回新文件句柄值.
int sys_dup2(unsigned int oldfd, unsigned int newfd)
{
	sys_close(newfd);               						// 若句柄 newfd 已经打开, 则首先关闭之.
	return dupfd(oldfd, newfd);      						// 复制并返回新句柄.
}

// 复制文件句柄系统调用.
// 复制指定文件句柄 fildes, 新句柄的值是当前最小的空闲句柄值.
// 参数: fildes -- 被复制的文件句柄.
// 返回新文件句柄值.
int sys_dup(unsigned int fildes)
{
	return dupfd(fildes, 0);
}

// 文件控制系统调用函数。
// 参数 fd 是文件句柄; cmd 是控制命令(参见 include/fcntl.h); arg 则针对不同的命令有不同的含义. 
// 对于复制句柄命令 F_DUFD, arg 是新文件句可取的最小值; 
// 对于设置文件操作和访问标志命令 F_SETFL, arg是新的文件操作和访问模式. 
// 对于文件上锁命令 F_GETLK, F_SETLK 和 F_SETLKW, arg 是指向 flock 结构的指针. 
// 但本内核中没有实现文件上锁功能.
// 返回: 若出错, 则所有操作都返回 -1. 若成功，那么 F_DUPFD 返回新文件句柄; 
// F_GETFD 返回文件句柄的当前执行时关闭标志 close_on_exec; F_GETFL 返回文件操作和访问标志.
int sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct file * filp;

	// 首先检查给出的文件句柄有效性. 然后根据不同命令 cmd 进行分别处理. 
	// 如果文件句柄值大于一个进程最多打开文件数 NR_OPEN, 或者该句柄的文件结构指针为空, 则返回出错码并退出.
	if (fd >= NR_OPEN || !(filp = current->filp[fd]))
		return -EBADF;
	switch (cmd) {
		case F_DUPFD:   										// 复制文件句柄.
			return dupfd(fd,arg);
		case F_GETFD:   										// 取文件句柄的执行时关闭标志.
			return (current->close_on_exec >> fd) & 1;
		case F_SETFD:   										// 设置执行时关闭标志. arg 位 0 置位是设置, 否则关闭.
			if (arg & 1)
				current->close_on_exec |= (1 << fd);
			else
				current->close_on_exec &= ~(1 << fd);
			return 0;
		case F_GETFL:   										// 取文件状态标志和访问模式.
			return filp->f_flags;
		case F_SETFL:   										// 设置文件状态和访问模式(根据 arg 设置添加, 非阻塞标志).
			filp->f_flags &= ~(O_APPEND | O_NONBLOCK);
			filp->f_flags |= arg & (O_APPEND | O_NONBLOCK);
			return 0;
		case F_GETLK:	case F_SETLK:	case F_SETLKW:  		// 未实现.
			return -1;
		default:
			return -1;
	}
}
