/*
 *  linux/kernel/chr_drv/tty_ioctl.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <termios.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>

// 根据进程组号 pgrp 取得进程组所属的会话号. 定义在 kernel/exit.c. 
extern int session_of_pgrp(int pgrp);
// 向使用指定 tty 终端的进程组中所有进程发送信号. 定义在 chr_drv/tty_io.c. 
extern int tty_signal(int sig, struct tty_struct *tty);

// 这是波特率因子数组(或称为除数数组). 波特率与波特率因子的对应关系参见列表后说明. 
// 例如波特率是 2400bit/s 时, 对应的因子是 48(0x300); 9600bit/s 的因子是 12(0x1c). 
static unsigned short quotient[] = {
	0, 2304, 1536, 1047, 857,
	768, 576, 384, 192, 96,
	64, 48, 24, 12, 6, 3
};

// 修改传输波特率. 
// 参数: tty - 终端对应的 tty 数据结构. 
// 在除数锁存标志 DLAB 置位情况下, 通过端口 0x3f8 和 0x3f9 向 UART 分别写入波特率因子低字节和高字节. 
// 写完后再复位 DLAB 位. 对于串口 2, 这两个端口分别是 0x2f8 和 0x2f9. 
static void change_speed(struct tty_struct * tty)
{
	unsigned short port,quot;

	// 函数首先检查参数 tty 指定的终端是否是串行终端, 若不是则退出. 对于串口终端的 tty 结构, 
	// 其读缓冲队列 data 字段存放着串行端口基址(0x3f8 或 0x2f8), 而一般控制台终端的 tty 结构的 read_q. 
	// data 字段值为 0. 然后从终端 termios 结构的控制模式标志集中取得已设置的波特率索引号, 
	// 并据此从波特率因子数组 quotient[] 中取得对应的波特率因子值 quot. 
	// CBAUD 是控制模式标志集中波特率位屏蔽码. 
	if (!(port = tty->read_q->data))
		return;
	quot = quotient[tty->termios.c_cflag & CBAUD];
	// 接着把波特率因子 quot 写入串行端口对应 UART 芯片的波特率因子锁存器中. 
	// 在写之前我们先要把线路控制寄存器 LCR 的除数锁存访问位 DLAB(位 7)置 1. 
	// 然后把 16 位的波特率因子低, 高字节分别写入端口 0x3f8, 0x3f9(分别对应波特率因子低, 高字节锁存器). 
	// 最后再复位 LCR 的 DLAB 标志位. 
	cli();
	outb_p(0x80, port + 3);									/* set DLAB */          // 首先设置除数锁定标志 DLAB. 
	outb_p(quot & 0xff, port);								/* LS of divisor */     // 输出因子低字节. 
	outb_p(quot >> 8, port + 1);							/* MS of divisor */     // 输出因子高字节. 
	outb(0x03, port + 3);									/* reset DLAB */        // 复位 DLAB. 
	sti();
}

// 刷新 tty 缓冲队列. 
// 参数: queue - 指定的缓冲队列指针. 
// 令缓冲队列的头指针等于尾指针, 从而达到清空缓冲区的目的. 
static void flush(struct tty_queue * queue)
{
	cli();
	queue->head = queue->tail;
	sti();
}

// 等待字符发送出去. 
static void wait_until_sent(struct tty_struct * tty)
{
	/* do nothing - not implemented */      /* 什么都没做 - 还未实现 */
}

// 发送 BREAK 控制符. 
static void send_break(struct tty_struct * tty)
{
	/* do nothing - not implemented */      /* 什么都没做 - 还未实现 */
}

// 取终端 termios 结构信息. 
// 参数: tty - 指定终端的 tty 结构指针; termios - 存放 termios 结构的用户缓冲区. 
static int get_termios(struct tty_struct * tty, struct termios * termios)
{
	int i;

	// 首先验证用户缓冲区指针所指内存区容量是否足够, 如不够则分配内存. 
	// 然后复制指定终端的 termios 结构信息到用户缓冲区中. 最后返回 0.
	verify_area(termios, sizeof (*termios));
	for (i = 0 ; i < (sizeof (*termios)) ; i++)
		put_fs_byte( ((char *) & tty->termios)[i] , i + (char *)termios );
	return 0;
}

// 设置终端 termios 结构信息. 
// 参数: tty - 指定终端的 tty 结构指针; termios - 用户数据区 termios 结构指针. 
static int set_termios(struct tty_struct * tty, struct termios * termios,
			int channel)
{
	int i, retsig;

	/* If we try to set the state of terminal and we're not in the
	   foreground, send a SIGTTOU.  If the signal is blocked or
	   ignored, go ahead and perform the operation.  POSIX 7.2) */
    /* 如果试图设置终端的状态但此时终端不在前台, 那么我们就需要发送一个 SIGTTOU 信号. 
	   如果该信号被进程屏蔽或者忽略了, 就直接执行本次操作. POSIX 7.2 */
	// 如果当前进程使用的 tty 终端的进程组号与进程的进程组号不同, 即当前进程终端不在前台, 表示当前进程试图修改不受控制的终端的 termios 结构. 
	// 因此根据 POSIX 标准的要求这里需要发送 SIGTTOU 信号让使用这个终端的进程暂时停止执行, 让我们先修改 termios 结构. 
	// 如果发送信号函数 tty_signal() 返回值是 ERESTARTSYS 或 EINTR, 则等一会儿再执行本次操作. 
	if ((current->tty == channel) && (tty->pgrp != current->pgrp)) {
		retsig = tty_signal(SIGTTOU, tty);
		if (retsig == -ERESTARTSYS || retsig == -EINTR)
			return retsig;
	}
	// 接着把用户数据区中 termios 结构信息复制到指定终端 tty 结构的 termios 结构中. 因为用户有可能已修改了终端串行口传输波特率, 
	// 所以这里再根据 termios 结构中的控制模式标志 c_cflag 中的波特率信息修改串行 UART 芯片内的传输波特率. 最后返回 0. 
	for (i = 0 ; i < (sizeof (*termios)) ; i++)
		((char *) & tty->termios)[i] = get_fs_byte(i + (char *)termios);
	change_speed(tty);
	return 0;
}

// 读取 termio 结构中的信息. 
// 参数: tty - 指定终端的 tty 结构指针; termio - 保存 termio 结构信息的用户缓冲区. 
static int get_termio(struct tty_struct * tty, struct termio * termio)
{
	int i;
	struct termio tmp_termio;

	// 首先验证用户的缓冲区指针所指内存区容量是否足够, 如不够则分配内存. 然后将 termios 结构的信息复制到临时 termio 结构中, 
	// 这两个结构基本相同, 输入, 输出, 控制和本地标志集数据类型不同. 前者的是 long, 而后者的是 short. 
	// 因此先复制到临时 termio 结构中目的是为了进行数据类型转换. 
	verify_area(termio, sizeof (*termio));
	tmp_termio.c_iflag = tty->termios.c_iflag;
	tmp_termio.c_oflag = tty->termios.c_oflag;
	tmp_termio.c_cflag = tty->termios.c_cflag;
	tmp_termio.c_lflag = tty->termios.c_lflag;
	tmp_termio.c_line = tty->termios.c_line;
	for(i = 0 ; i < NCC ; i++)
		tmp_termio.c_cc[i] = tty->termios.c_cc[i];
	// 然后逐字节地把临时 termio 结构中的信息复制到用户 termio 结构缓冲区中. 并返回 0. 
	for (i = 0 ; i < (sizeof (*termio)) ; i++)
		put_fs_byte( ((char *) & tmp_termio)[i] , i + (char *)termio );
	return 0;
}

/*
 * This only works as the 386 is low-byt-first
 */
/*
 * 下面 termio 设置函数仅适用于低字节在前的 386CPU. 
 */
// 设置终端 termio 结构信息. 
// 参数: tty - 指定终端的 tty 结构指针; termio - 用户数据区中 termio 结构. 
// 将用户缓冲区 termio 的信息复制到终端的 termios 结构中. 返回 0. 
static int set_termio(struct tty_struct * tty, struct termio * termio,
			int channel)
{
	int i, retsig;
	struct termio tmp_termio;

	// 与 set_termios() 一样, 如果进程使用的终端的进程组号的进程组号与进程的进程组号不同, 即当前进程终端不在前台, 
	// 表示当前进程试图修改不受控制的终端的 termios 结构. 
	// 因此根据 POSIX 标准的要求这里需要发送 SIGTTOU 信号让使用这个终端的进程先暂时停止执行, 以让我们先修改 termios 结构. 
	// 如果发送信号函数 tty_signal() 返回值是 ERESTARTSYS 或 EINTR, 则等一会再执行本次操作. 
	if ((current->tty == channel) && (tty->pgrp != current->pgrp)) {
		retsig = tty_signal(SIGTTOU, tty);
		if (retsig == -ERESTARTSYS || retsig == -EINTR)
			return retsig;
	}
	// 接着复制用户数据区中 termio 结构信息到临时 termio 结构中. 然后再将 termio 结构的信息复制到 tty 的 termios 结构中. 
	// 这样做的目的是为了对其中模式标志集的类型进行转换, 即从 termio 的短整数类型转换成 termios 的长整数类型. 
	// 但两种结构的 c_line 和 c_cc[] 字段是完全相同的. 
	for (i = 0 ; i< (sizeof (*termio)) ; i++)
		((char *)&tmp_termio)[i] = get_fs_byte(i + (char *)termio);
	*(unsigned short *)&tty->termios.c_iflag = tmp_termio.c_iflag;
	*(unsigned short *)&tty->termios.c_oflag = tmp_termio.c_oflag;
	*(unsigned short *)&tty->termios.c_cflag = tmp_termio.c_cflag;
	*(unsigned short *)&tty->termios.c_lflag = tmp_termio.c_lflag;
	tty->termios.c_line = tmp_termio.c_line;
	for(i = 0 ; i < NCC ; i++)
		tty->termios.c_cc[i] = tmp_termio.c_cc[i];
	// 最后因为用户有可能已修改了终端串行口传输波特率, 
	// 所以这里再根据 termios 结构中的控制模式标志 c_cflag 中的波特率信息修改串行 UART 芯片内的传输波特率, 并返回 0. 
	change_speed(tty);
	return 0;
}

// tty 终端设备输入输出控制函数. 
// 参数: dev - 设备号; cmd - ioctl 命令; arg - 操作参数指针. 
// 该函数首先根据参数给出的设备号找出对应终端的 tty 结构, 然后根据控制命令 cmd 分别进行处理. 
int tty_ioctl(int dev, int cmd, int arg)
{
	struct tty_struct * tty;
	int	pgrp;

	// 首先根据设备号取得 tty 子设备号, 从而取得终端的 tty 结构. 若主设备号是 5(控制终端), 则进程的 tty 字段即是 tty 子设备号. 
	// 此时如果进程的 tty 子设备号是负数, 表明该进程没有控制终端, 即不能发出该 ioctl 调用, 于是显示出错信息并停机. 
	// 如果主设备号不是 5 而是 4, 我们就可以从设备号中取出子设备号. 子设备号可以是 0(控制台终端), 1(串口 1 终端), 2(串口 2 终端). 
	if (MAJOR(dev) == 5) {
		dev = current->tty;
		if (dev < 0)
			panic("tty_ioctl: dev<0");
	} else
		dev = MINOR(dev);
	// 然后根据子设备号和 tty 表, 我们可以取得对应终端的 tty 结构. 于是让 tty 指向对应子设备号的 tty 结构. 
	// 然后再根据参数提供的 ioctl 命令 cmd 进行分别处理. 144 行后半部分用于根据子设备号 dev 在 tty_table[] 表中选择对应的 tty 结构. 
	// 如果 dev = 0, 表示正在使用前台终端, 因此直接使用终端号 fg_console 作为 tty_table[] 项索引取 tty 结构. 
	// 如果 dev 大于 0, 那么就要分两种情况考虑: 
	// 1: dev 是虚拟终端号; 2: dev 是串行终端号或者伪终端号. 对于虚拟终端其 tty 结构在 tty_table[] 中索引项是 dev-1(0--63). 
	// 对于其他类型终端, 则它们的 tty 结构索引项就是 dev. 例如, 如果 dev = 64, 表示是一个串行终端 1, 则其 tty 结构就是 tty_table[dev], 
	// 如果 dev = 1, 则对应终端的 tty 结构是 tty_table[0]. 
	tty = tty_table + (dev ? ((dev < 64)? dev - 1 : dev) : fg_console);
	switch (cmd) {
		// 取相应终端termios结构信息. 此时参数arg是用户缓冲区指针. 
		case TCGETS:
			return get_termios(tty, (struct termios *) arg);
		// 在设置 termios 结构信息之前, 需要先等待输出队列中所有数据处理完毕, 并且刷新(清空)输入队列. 
		// 再接着执行下面的设置终端 termios 结构的操作. 
		case TCSETSF:
			flush(tty->read_q); 							/* fallthrough */
		// 在设置终端 termios 的信息之前, 需要先等待输出队列中所有数据处理完(耗尽). 对于修改参数会影响输出的情况, 就需要使用这种形式. 
		case TCSETSW:
			wait_until_sent(tty); 							/* fallthrough */
		// 设置相应终端 termios 结构信息. 此时参数 arg 是保存 termios 结构的用户缓冲区指针. 
		case TCSETS:
			return set_termios(tty,(struct termios *) arg, dev);
		// 取相应终端 termio 结构中的信息. 此时参数 arg 是用户缓冲区指针. 
		case TCGETA:
			return get_termio(tty,(struct termio *) arg);
		// 在设置 termio 结构信息之前, 需要先等待输出队列中所有数据处理完毕, 并且刷新(清空)输入队列. 
		// 再接着执行下面的设置终端 termio 结构的操作. 
		case TCSETAF:
			flush(tty->read_q); 							/* fallthrough */
		// 在设置终端 termios 的信息之前, 需要先等待输出队列中所有数据处理完(耗尽). 对于修改参数会影响输出的情况, 就需要使用这种形式. 
		case TCSETAW:
			wait_until_sent(tty); 							/* fallthrough */
		// 设置相应终端 termio 结构信息. 此时参数 arg 是保存 termio 结构的用户缓冲区指针. 
		case TCSETA:
			return set_termio(tty,(struct termio *) arg, dev);
		// 如果参数 arg 值是 0, 则等待输出队列处理完毕(空), 并发送一个 break. 
		case TCSBRK:
			if (!arg) {
				wait_until_sent(tty);
				send_break(tty);
			}
			return 0;
		// 开始/停止流控制. 如果参数 arg 是 TCOOFF(Terminal Control Output OFF), 则挂起输出; 如果是 TCOON, 则恢复挂起的输出. 
		// 在挂起或恢复输出同时需要把写队列中的字符输出, 以加快用户交互响应速度. 如果 arg 是 TCIOFF(Terminal Control Input ON), 
		// 则挂起输入; 如果是 TCION, 则重新开启挂起的输入. 
		case TCXONC:
			switch (arg) {
			case TCOOFF:
				tty->stopped = 1;       					// 停止终端输出. 
				tty->write(tty);        					// 写缓冲队列输出. 
				return 0;
			case TCOON:
				tty->stopped = 0;       					// 恢复终端输出. 
				tty->write(tty);
				return 0;
			// 如果参数 arg 是 TCIOFF, 表示要求终端停止输入, 于是我们往终端写队列放入 STOP 字符. 当终端收到该字符时就会暂停输入. 
			// 如果参数是 TCION, 表示发送一个 START 字符, 让终端恢复传输. STOP_CHAR(tty) 字义为((tty)->termios.c_cc[VSTOP]), 
			// 即取终端 termios 结构控制字符数组对应项值. 若内核定义了 _POSIX_VDISABLE(\0), 那么当某一项等于 _POSIX_VDISABLE 的值时, 
			// 表示禁止使用相应的特殊字符. 因此这里直接判断该值是否为 0 来确定要不要把停止控制字符放入终端写队列中. 以下同. 
			case TCIOFF:
				if (STOP_CHAR(tty))
					PUTCH(STOP_CHAR(tty), tty->write_q);
				return 0;
			case TCION:
				if (START_CHAR(tty))
					PUTCH(START_CHAR(tty), tty->write_q);
				return 0;
			}
			return -EINVAL; 								/* not implemented */
		// 刷新已写输出但还没有发送, 或已接收但还没有读的数据. 如果参数 arg 是 0, 则刷新(清空)输入队列; 
		// 如果是 1, 则刷新输出队列; 如果 2, 则刷新输入和输出队列. 
		case TCFLSH:
			if (arg == 0)
				flush(tty->read_q);
			else if (arg == 1)
				flush(tty->write_q);
			else if (arg == 2) {
				flush(tty->read_q);
				flush(tty->write_q);
			} else
				return -EINVAL;
			return 0;
		// 设置终端串行线路专用模式. 
		case TIOCEXCL:
			return -EINVAL; 							/* not implemented */   /* 未实现 */
		// 复位终端串行线路专用模式. 
		case TIOCNXCL:
			return -EINVAL; 							/* not implemented */
		// 设置 tty 为控制终端. (TIOCNOTTY - 不要控制终端). 
		case TIOCSCTTY:
			return -EINVAL; 							/* set controlling term NI */
		// 读取终端进程组号(即读取前台进程组号). 首先验证用户缓冲区长度, 然后复制终端 tty 的 pgrp 字段到用户缓冲区. 
		// 此时参数 arg 是用户缓冲区指针. 
		case TIOCGPGRP:
			verify_area((void *) arg, 4);            	// 实现库函数 tcgetpgrp(). 
			put_fs_long(tty->pgrp, (unsigned long *) arg);
			return 0;
		// 设置终端进程组号 pgrp(即设置前台进程组号). 此时参数 arg 是用户缓冲区中进程组号 pgrp 的指针. 
		// 执行该命令的前提条件是进程必须有控制终端. 如果当前进程没有控制终端, 或者 dev 不是其控制终端, 
		// 或者控制终端现在的确是正在处理的终端 dev, 但进程的会话号与该终端 dev 的会话号不同, 则返回无终端错误信息. 
		case TIOCSPGRP:                                 // 实现库函数 tcsetpgrp(). 
			if ((current->tty < 0) ||
			    (current->tty != dev) ||
			    (tty->session != current->session))
				return -ENOTTY;
			// 然后我们就从用户缓冲区中取得欲设置的进程号, 并对该组号的有效性进行验证. 如果组号 pgrp 小于 0, 则返回无效组号错误信息; 
			// 如果 pgrp 的会话号与当前进程的不同, 则返回许可错误信息. 否则我们可以设置终端进程组号为 pgrp. 此时 pgrp 成为前台进程组. 
			pgrp = get_fs_long((unsigned long *) arg);
			if (pgrp < 0)
				return -EINVAL;
			if (session_of_pgrp(pgrp) != current->session)
				return -EPERM;
			tty->pgrp = pgrp;
			return 0;
		// 返回输出队列中还未送出的字符数. 首先验证用户缓冲区长度, 然后复制队列中字符数给用户. 此时参数 arg 是用户缓冲区指针. 
		case TIOCOUTQ:
			verify_area((void *) arg, 4);
			put_fs_long(CHARS(tty->write_q), (unsigned long *) arg);
			return 0;
		// 返回输入队列中还未读取的字符数. 首先验证用户缓冲区长度, 然后复制队列中字符数给用户. 此时参数 arg 是用户缓冲区指针. 
		case TIOCINQ:
			verify_area((void *) arg, 4);
			put_fs_long(CHARS(tty->secondary),
				(unsigned long *) arg);
			return 0;
		// 模拟终端输入操作. 该命令以一个指向字符的指针作为参数, 并假设该字符是在终端上键入的. 
		// 用户终须在该控制终端上具有超级用户权限或具有读许可权限. 
		case TIOCSTI:
			return -EINVAL; 							/* not implemented */
		// 读取终端设备窗口大小信息(参见 termios.h 中的 winsize 结构). 
		case TIOCGWINSZ:
			return -EINVAL; 							/* not implemented */
		// 设置终端设备窗口大小信息(参见 winsize 结构). 
		case TIOCSWINSZ:
			return -EINVAL; 							/* not implemented */
		// 返回 MODEM 状态控制引线的当前状态位标志集(参见 termios.h). 
		case TIOCMGET:
			return -EINVAL; 							/* not implemented */
		// 设置单个 modem 状态控制引线的状态(true 或 false). 
		case TIOCMBIS:
			return -EINVAL; 							/* not implemented */
		// 复位单个 MODEM 状态控制引线的状态. 
		case TIOCMBIC:
			return -EINVAL; 							/* not implemented */
		// 设置 MODEM 状态引线的状态. 如果某一位置位, 则 modem 对应的状态引线将为有效. 
		case TIOCMSET:
			return -EINVAL; 							/* not implemented */
		// 读取软件载波检测标志(1 - 开启; 0 - 关闭). 
		case TIOCGSOFTCAR:
			return -EINVAL; 							/* not implemented */
		// 设置软件载波检测标志(1 - 开启; 0 - 关闭). 
		case TIOCSSOFTCAR:
			return -EINVAL; 							/* not implemented */
		default:
			return -EINVAL;
        }
}
