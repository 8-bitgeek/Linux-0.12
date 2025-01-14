/*
 *  linux/fs/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

//#include <string.h>
#include <errno.h>									// 错误号头文件. 包含系统中各种出错号.
#include <fcntl.h>									// 文件控制头文件. 用于文件及其描述符操作控制常数符号定义.
#include <sys/types.h>
#include <utime.h>
#include <sys/stat.h>								// 文件状态头文件. 含有文件状态结构 stat{} 和符号常量等.

#include <linux/sched.h>							// 调度程序头文件, 定义任务结构 task_struct, 任务 0 数据等.
#include <linux/tty.h>								// tty 头文件, 定义了有关 tty_io, 串行通信方面的参数, 常数.
#include <linux/kernel.h>

#include <asm/segment.h>

// 取文件系统信息. 
// 参数 dev 是含有用户已安装文件系统的设备号. ubuf 是一个 ustat 结构缓冲区指针, 用于存放系统返回的文件系统信息. 
// 该系统调用用于返回已安装(mounted)文件系统的统计信息. 
// 成功时返回 0, 并且 ubuf 指向的 ustate 结构被添入文件系统总空闲块和空闲 i 节点数. 
// ustat 结构定义在 include/sys/types.h 中. 
int sys_ustat(int dev, struct ustat * ubuf)
{
	return -ENOSYS;         						// 出错码: 功能还未实现. 
}

// 设置文件访问和修改时间. 
// 参数 filename 是文件名, times 是访问和修改时间结构指针. 
// 如果 times 指针不为 NULL, 则取 utimbuf 结构中的时间信息来设置文件的访问和修改时间. 
// 如果 times 指针是 NULL, 则取系统当前时间来设置指定文件的访问和修改时间域. 
int sys_utime(char * filename, struct utimbuf * times)
{
	struct m_inode * inode;
	long actime, modtime;

	// 文件的时间信息保存在其 i 节点中. 因此我们首先根据文件名取得对应 i 节点. 如果没有找到, 则返回出错码. 
	// 如果提供的访问和修改时间结构指针 times 不为 NULL, 则从结构中读取用户设置的时间值. 
	// 否则就用系统当前时间来设置文件的访问和修改时间. 
	if (!(inode = namei(filename)))
		return -ENOENT;
	if (times) {
		actime = get_fs_long((unsigned long *) &times->actime);
		modtime = get_fs_long((unsigned long *) &times->modtime);
	} else
		actime = modtime = CURRENT_TIME;
	// 然后修改 i 节点中的访问时间字段和修改时间字段. 再设置 i 节点已修改标志, 放回该 i 节点, 并返回 0. 
	inode->i_atime = actime;
	inode->i_mtime = modtime;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

/*
 * XXX should we use the real or effective uid?  BSD uses the real uid,
 * so as to make this call useful to setuid programs.
 */
/*
 * XXX 我们该用真实用户 id(ruid) 还是有效有户 id(euid)? BSD 系统使用了真实用户 id, 以使该调用可以供 setuid 程序使用. 
 * (注: POSIX 标准建议使用真实用户 ID). 
 * (注 1: 英文注释开始的 ‘XXX’ 表示重要提示). 
 */
// 检查文件的访问权限. 
// 参数 filename 是文件名, mode 是检查的访问属性, 
// 它有 3 个有效位组成: R_OK(值 4), W_OK(2), X_OK(1) 和 F_OK(0)组成, 
// 分别表示检测文件是否可读, 可写, 可执行和文件是否存在. 如果访问允许的话, 则返回 0, 否则返回出错码. 
int sys_access(const char * filename, int mode)
{
	struct m_inode * inode;
	int res, i_mode;

	// 文件的访问权限信息同样保存在文件的 i 节点结构中, 因此我们要先取得对应文件名的 i 节点. 
	// 检测的访问属性 mode 由低 3 位组成, 因此需要与上八进制 0007 来清除所有高位. 
	// 如果文件名对应的 i 节点不存在, 则返回没有许可权限出错码. 若 i 节点存在, 则取 i 节点中文件属性码, 
	// 并放回该 i 节点. 另外, 57 行上语句 "iput(inode);" 最好放在 61 行之后. 
	mode &= 0007;
	if (!(inode = namei(filename)))
		return -EACCES;                 				// 出错码: 无访问权限. 
	i_mode = res = inode->i_mode & 0777;
	iput(inode);
	// 如果当前进程用户是该文件的宿主, 则取文件宿主属性. 否则如果当前进程用户与该文件宿主同属一个级, 则取文件组属性. 
	// 否则此时 res 低 3 位是其他人访问该文件的许可属性. 
	// [?? 这里应 res >> 3 ??]
	if (current->uid == inode->i_uid)
		res >>= 6;
	else if (current->gid == inode->i_gid)
		res >>= 3;
	// 此时 res 的最低 3 位是根据当前进程用户与文件的关系选择出来的访问属性位. 
	// 现在我们来判断这 3 位. 如果文件属性具有参数所查询的属性位 mode, 则访问许可, 返回 0. 
	if ((res & 0007 & mode) == mode)
		return 0;
	/*
	 * XXX we are doing this test last because we really should be
	 * swapping the effective with the real user id (temporarily),
	 * and then calling suser() routine.  If we do call the
	 * suser() routine, it needs to be called last.
	 */
    /*
     * XXX 我们最后才做下面的测试, 因为我们实际上需要交换有效用户 ID 和真实用户 ID(临时地), 
	 * 然后才调用 suser() 函数, 如果我们确实要调用 suser() 函数, 则需要最后才被调用. 
     */
	// 如果当前用户 ID 为 0(超级用户) 并且屏蔽码执行位是 0 或者文件可以被任何人执行, 搜索, 则返回 0. 否则返回出错码. 
	if ((!current->uid) &&
	    (!(mode & 1) || (i_mode & 0111)))
		return 0;
	return -EACCES;         							// 出错码: 无访问权限. 
}

// 改变当前工作目录系统调用. 
// 参数 filename 是目录名. 
// 操作成功则返回 0, 否则返回出错码. 
int sys_chdir(const char * filename)
{
	struct m_inode * inode;

	// 改变当前工作目录就是要求把进程任务结构的当前工作目录字段指向给定目录名的 i 节点. 
	// 因此我们首先取目录名的 i 节点. 如果目录名对应的 i 节点不存在, 则返回出错码. 
	// 如果该 i 节点不是一个目录 i 节点, 则放回该 i 节点, 并返回出错码. 
	if (!(inode = namei(filename)))
		return -ENOENT;                 				// 出错码: 文件或目录不存在. 
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;                				// 出错码: 不是目录名. 
	}
	// 然后释放进程原工作目录 i 节点, 并使其指向新设置的工作目录 i 节点. 返回 0.
	iput(current->pwd);
	current->pwd = inode;
	return (0);
}

// 改变根目录系统调用. 
// 把指定的目录名设置成为当前进程的根目录 "/". 
// 如果操作成功则返回 0, 否则返回出错码. 
int sys_chroot(const char * filename)
{
	struct m_inode * inode;

	// 该调用用于改变当前进程任务结构中的根目录字段 root, 让其指向参数给定目录名的 i 节点. 
	// 如果目录名对应 i 节点不存在, 则返回出错码. 
	// 如果该 i 节点不是目录 i 节点, 则放回该 i 节点, 也返回出错码. 
	if (!(inode = namei(filename)))
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	// 然后释放当前进程的根目录, 并重新设置为指定目录名的 i 节点, 返回 0. 
	iput(current->root);
	current->root = inode;
	return (0);
}

// 修改文件属性系统调用. 
// 参数 filename 是文件名, mode 是新的文件属性. 
int sys_chmod(const char * filename, int mode)
{
	struct m_inode * inode;

	// 该调用为指定文件设置新的访问属性 mode. 
	// 文件的访问属性在文件名对应的 i 节点中, 因此我们首先取文件名对应的 i 节点. 
	// 如果 i 节点不存在, 则返回出错码(文件或目录不存在). 
	// 如果当前进程的有效用户名 id 与文件 i 节点的用户 id 不同, 并且也不是超级用户, 
	// 则放回该文件 i 节点, 返回出错码(没有访问权限). 
	if (!(inode = namei(filename)))
		return -ENOENT;
	if ((current->euid != inode->i_uid) && !suser()) {
		iput(inode);
		return -EACCES;
	}
	// 否则就重新设置该 i 节点的文件属性, 并置该 i 节点已修改标志. 放回该 i 节点, 返回 0. 
	inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

// 修改文件宿主系统调用. 
// 参数 filename 是文件名, uid 是用户标识符(用户 ID), gid 是组 ID. 
// 若操作成功则返回 0, 否则返回出错码. 
int sys_chown(const char * filename, int uid, int gid)
{
	struct m_inode * inode;

	// 该调用用于设置文件 i 节点中的用户和组 ID, 因此首先要取得给定文件名的 i 节点. 
	// 如果文件名的 i 节点不存在, 则返回出错码(文件或目录不存在). 
	// 如果当前进程不是超级用户, 则放回该 i 节点, 并返回出错码(没有访问权限). 
	if (!(inode = namei(filename)))
		return -ENOENT;
	if (!suser()) {
		iput(inode);
		return -EACCES;
	}
	// 否则我们就用参数提供的值来设置文件 i 节点的用户 ID 和组 ID, 并置 i 节点已经修改标志, 放回该 i 节点, 返回 0. 
	inode->i_uid = uid;
	inode->i_gid = gid;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

// 检查字符设备. tty: Teletype, 电传打字机.
// 该函数仅用于下面文件打开系统调用 sys_open(), 用于检查若打开的文件是 tty 终端字符设备时, 
// 对当前进程和 tty 表进行相应设置. 返回 0 检测处理成功, 返回 -1 表示失败, 对应字符设备不能打开.
static int check_char_dev(struct m_inode * inode, int dev, int flag)
{
	struct tty_struct * tty;
	int min;										// 子设备号.

	// 对于一个进程来说, 如果 tty 字段不为 -1, 则表示有对应的 tty 终端, tty 字段即子设备号.
	// 如果打开操作的文件是 /dev/tty(即 MAJOR(dev) = 5), 那么我们令 min = 进程的 tty, 
	// 如果打开的是 ttyx 设备, 则直接取其子设备号. 如果得到的子设备号小于 0, 
	// 那么说明进程没有控制终端, 或者设备号错误, 则返回 -1, 表示进程没有控制终端或者设备号错误.
	if (MAJOR(dev) == 4 || MAJOR(dev) == 5) { 		// 只检查主设备是 ttyx 或者 tty 的设备.
		if (MAJOR(dev) == 5) 						// 5 - tty, 4 - ttyx.
			min = current->tty; 					// 如果设备号是 tty, 则子设备号为当前任务的 tty(-1 表示没有).
		else
			min = MINOR(dev);
		if (min < 0) 								// 出错.
			return -1;
		// 主伪终端设备文件只能被进程独占使用, 即设备文件的 inode->i_count 不能大于 1, 如果 > 1 则出错. 
		if (IS_A_PTY_MASTER(min) && inode->i_count > 1) 	// 如果引用次数 > 1 则表示被两个地方引用, 不是独占的.
			return -1;
		// 我们让 tty 结构指针指向 tty 表中对应结构项.
		tty = TTY_TABLE(min); 								// /dev/tty1 对应 tty_table[0].
		// 若文件访问标志 flag 中没有表明不需要分配终端(O_NOCTTY), 并且当前进程是进程组首领, 
		// 并且当前进程还没有控制终端, 并且 tty 中 session 字段为 0(表示该终端还没分配给其它进程), 
		// 那么就将这个 tty 设置为当前进程的终端(current->tty = min).
		// 并且将该 tty 的 session 和 pgrp 分别设置为当前进程的会话号和进程组号(绑定).
		if (!(flag & O_NOCTTY) && current->leader && current->tty < 0 && tty->session == 0) {
			current->tty = min; 								// 设置当前进程的终端号.
			tty->session = current->session; 					// 将 tty 的会话号设置为当前进程的会话号.
			tty->pgrp = current->pgrp; 							// 设置终端对应的进程组号.
		}
		// 如果文件访问标志 flag 中含有 O_NONBLOCK(非阻塞)标志, 则我们需要对该字符终端设备进行相关设置, 
		// 设置为满足读操作需要读取的最少字符数为 0, 设置超时定时值为 0, 并把终端设备设置成非规范模式. 
		// 非阻塞方式只能工作于非规范模式. 在此模式下当 VMIN 和 VTIME 均设置为 0 时, 
		// 辅助队列中有多少字符进程就读取多少字符, 并立刻返回.
		if (flag & O_NONBLOCK) {
			TTY_TABLE(min)->termios.c_cc[VMIN] = 0;
			TTY_TABLE(min)->termios.c_cc[VTIME] = 0;
			TTY_TABLE(min)->termios.c_lflag &= ~ICANON;
		}
	}
	return 0;
}

// 打开(或创建)文件的系统调用(实际就是从硬盘中读取或创建这个文件名对应的 inode, 并在进程的文件列表中添加一个文件项来指向这个 inode).
// 参数 filename 是文件名, flag 是文件访问标志, 它可取值: O_RDONLY(00, 只读), O_WRONLY(01, 只写), 
// O_RDWR(02, 读写), 以及 O_CREAT(00100, 不存在则创建), O_EXCL(00200, 被创建文件必须不存在), 
// O_APPEND(在文件尾添加数据) 等其他一些标志的组合(include/fcntl.h).
// 如果本调用创建了一个新文件, 则 mode 就用于指定文件的许可属性. 
// 这些属性有 S_IRWXU(00700, 文件宿主具有读, 写和执行权限), S_IRUSR(00400, 用户具有读文件权限), 
// S_IRWXG(00070, 组成员有读, 写执行)等等. 
// 对于新创建的文件, 这些属性只应用于将来对文件的访问, 创建了只读文件的打开调用也将返回一个读写的文件句柄. 
// 如果调用操作成功, 则返回文件句柄(文件描述符 fd), 否则返回出错码. 参见(include/sys/stat.h include/fcntl.h).
int sys_open(const char * filename, int flag, int mode)
{
	struct m_inode * inode;
	struct file * f;
	int i, fd;

	// 首先对参数进行处理. 将用户设置的文件模式和进程模式屏蔽码相与, 产生许可的文件模式.     rwx-rwx-rwx
	mode &= (0777 & ~current->umask); 	// 如果 umask = 000010010: ~(000-010-010) = 111-101-101 & 111-111-111 = 111-101-101: 即屏蔽掉组成员和其他人的 w 权限.
	// 为了给打开文件建立一个文件句柄, 需要在进程的文件列表中找到一个空闲项.
	// 空闲项的索引号 fd 即是句柄值. 若没有找到空闲项, 则返回出错码(参数无效).
	for (fd = 0; fd < NR_OPEN; fd++)
		if (!current->filp[fd]) 					// 文件指针为空则表示找到空闲项.
			break;
	if (fd >= NR_OPEN) 								// 没找到空闲项则返回出错码.
		return -EINVAL;
	// 复位 fd 对应的 close_on_exec 位标志, close_on_exec 中的每个位(置位)代表系统调用 execve() 时需要关闭的文件句柄. 
	// 当程序使用 fork() 函数创建一个子进程时, 通常会在该子进程中调用 execve() 函数加载执行另一个新程序. 
	// 此时子进程中开始执行新程序, 如果 close_on_exec 中的有被置位的文件句柄, 则会关闭这个文件.
	// 当打开一个文件时, 默认情况下文件句柄要处于打开的状态, 因此这里要复位对应位, 
	// 然后为打开的文件在系统文件表 file_table 中寻找一个空闲结构项. 
	current->close_on_exec &= ~(1 << fd);           // 复位对应文件打开位, 不让其在执行 execve 时关闭.
	// 令 f 指向系统的文件列表开始处, 搜索空闲文件项(引用计数为 0 的项), 若已经没有空闲文件表结构项, 则返回出错码. 
	// 另外, 下面的指针赋值 "f = 0 + file_table" 等同于 "f = file_table" 和 "f = &file_table[0]". 
	f = 0 + file_table; 							// (fs/file_table.c)
	for (i = 0; i < NR_FILE; i++, f++)
		if (!f->f_count) break;         			// 在系统文件表中找到空闲结构项(没有被引用的文件项). 
	if (i >= NR_FILE)
		return -EINVAL;
	// 此时我们让进程文件列表中 fd 文件指针指向系统的文件列表中的空闲项, 并令文件引用计数递增 1. 
	(current->filp[fd] = f)->f_count++;
	// Log(LOG_INFO_TYPE, "<<<<< sys_open: fd = %d >>>>>\n", fd);
	// 调用函数 open_namei() 执行打开 inode 操作, 若返回值小于 0, 则说明出错, 于是释放刚申请到的文件结构, 返回出错码 i.
	// 所谓打开 inode 是指从硬盘中读取或新建这个文件名对应的 inode 信息, 将其指针放到 inode 变量中(打开成功的情况下).
	if ((i = open_namei(filename, flag, mode, &inode)) < 0) { 		// 如果出错则进行相应处理后返回出错码.
		current->filp[fd] = NULL;
		f->f_count = 0;
		return i;
	}
	// 根据文件 inode 的属性字段, 可以判断文件的类型. 对于特殊类型的文件作一些特殊处理. 
	// 如果打开的是字符设备文件, 则尝试为该进程绑定这个字符设备(current->tty 指向该设备)(如果允许的话).
	// 如果不允许打开使用该字符设备文件, 那么我们只能释放上面申请的文件项和句柄资源. 返回出错码.
	/* ttys are somewhat special (ttyx major==4, tty major==5) */
	if (S_ISCHR(inode->i_mode)) 					// 如果是字符设备文件(比如 /dev/tty1 等: 终端设备, 内存设备, 网络设备).
		if (check_char_dev(inode, inode->i_zone[0], flag)) { 	// 设备文件的 zone[0] 中存放的是设备号.
			iput(inode);
			current->filp[fd] = NULL;
			f->f_count = 0;
			return -EAGAIN;         							// 出错号: 资源暂不可用.
		}
	/* Likewise with block-devices: check for floppy_change */
	// 如果打开的是块设备文件, 则检查盘片是否更换过. 若更换过则需要让高速缓冲区中该设备的所有缓冲块失效.
	if (S_ISBLK(inode->i_mode))
		check_disk_change(inode->i_zone[0]);
	// 初始化打开文件的文件结构: 设置文件结构属性和标志, 置句柄引用计数为 1, 
	// 并设置 inode 字段为打开的 inode, 初始化文件读写指针为 0. 最后返回文件句柄号.
	f->f_mode = inode->i_mode;
	f->f_flags = flag;
	f->f_count = 1;
	f->f_inode = inode;											// 文件与 inode 关联.
	f->f_pos = 0;
	return fd;
}

// 创建文件系统调用. 
// 参数 pathname 是路径名, mode 与上面的 sys_open() 函数相同. 
// 成功则返回文件句柄, 否则返回出错码. 
int sys_creat(const char * pathname, int mode)
{
	return sys_open(pathname, O_CREAT | O_TRUNC, mode);
}

// 关闭文件系统调用.
// 参数 fd 是当前进程要关闭的文件句柄.
// 成功则返回 0, 否则返回出错码.
int sys_close(unsigned int fd)
{
	struct file * filp;

	// 首先检查参数有效性. 若给出的文件句柄值大于进程的最大打开文件数 NR_OPEN, 则返回出错码(参数无效).
	if (fd >= NR_OPEN)
		return -EINVAL;
	// 然后复位该句柄对应的 close_on_exec 位. 
	current->close_on_exec &= ~(1 << fd);
	// 若该文件句柄对应的文件指针是 NULL, 则返回出错码.
	if (!(filp = current->filp[fd]))
		return -EINVAL;
	// 将文件句柄对应的文件指针置为 NULL(sys_close() 最关键的一步).
	// 若在关闭文件之前, 对应文件结构中的句柄引用计数已经为 0, 则说明内核出错, 停机. 
	// 否则将对应文件的引用计数减 1. 此时如果它还不为 0, 则说明有其它进程正在使用该文件, 直接返回 0(成功).
	// 如果引用计数已等于 0, 说明该文件已经没有进程引用, 该文件已变为空闲. 则释放该文件对应的 inode, 然后返回 0.
	current->filp[fd] = NULL;
	if (filp->f_count == 0)
		panic("Close: file count is 0");
	if (--filp->f_count)
		return (0);
	iput(filp->f_inode);
	return (0);
}
