/*
 *  linux/kernel/tty_io.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'tty_io.c' gives an odo_tty_interruptrthogonal feeling to tty's, be they consoles
 * or rs-channels. It also implements echoing, cooked mode etc.
 *
 * Kill-line thanks to John T Kohl, who also corrected VMIN = VTIME = 0.
 */
/*
 * 'tty_io.c' 给 tty 终端一种非相关的感觉, 不管它们是控制台还是串行终端.
 * 该程序同样实现了回显, 规范(熟)模式等.
 */

#include <ctype.h>										// 字符类型头文件. 定义了一些有关字符类型判断的转换的宏.
#include <errno.h>
#include <signal.h>
#include <unistd.h>										// unistd.h 是标准符号常数与类型文件, 并声明了各种函数.

// 给出定时警告(alarm)信号在信号位图中对应的位屏蔽位. 
#define ALRMMASK (1<<(SIGALRM-1))

#include <linux/sched.h>
#include <linux/tty.h>									// tty 头文件, 定义了有关 tty_io, 串行通信方面的参数, 常数.
#include <asm/segment.h>								// 段操作头文件. 定义了有关段寄存器操作的嵌入式汇编函数.
#include <asm/system.h>									// 系统头文件. 定义设置或修改描述符/中断门等嵌入式汇编宏.

int kill_pg(int pgrp, int sig, int priv);
int is_orphaned_pgrp(int pgrp);                 		// 判断是否孤儿进程.

// 获取 termios 结构中三个模式标志集之一, 或者用于判断一个标志集是否有置位标志.
#define _L_FLAG(tty, f)	((tty)->termios.c_lflag & f)	// 本地模式标志.
#define _I_FLAG(tty, f)	((tty)->termios.c_iflag & f)	// 输入模式标志.
#define _O_FLAG(tty, f)	((tty)->termios.c_oflag & f)	// 输出模式标志.

// 取 termios 结构终端特殊(本地)模式标志集中的一个标志.
#define L_CANON(tty)	_L_FLAG((tty), ICANON)			// 取规范模式标志.
#define L_ISIG(tty)		_L_FLAG((tty), ISIG)			// 取信号标志.
#define L_ECHO(tty)		_L_FLAG((tty), ECHO)			// 取回显字符标志.
#define L_ECHOE(tty)	_L_FLAG((tty), ECHOE)			// 规范模式时取回显擦除标志.
#define L_ECHOK(tty)	_L_FLAG((tty), ECHOK)			// 规范模式时取 KILL 擦除当前行标志.
#define L_ECHOCTL(tty)	_L_FLAG((tty), ECHOCTL)			// 取回显控制字符标志.
#define L_ECHOKE(tty)	_L_FLAG((tty), ECHOKE)			// 规范模式时取 KILL 擦除行并回显标志.
#define L_TOSTOP(tty)	_L_FLAG((tty), TOSTOP)			// 对于后台输出发送 SIGTTOU 信号.

// 取 termios 结构输入模式标志集中的一个标志.
#define I_UCLC(tty)		_I_FLAG((tty), IUCLC)			// 取大写到小写转换标志.
#define I_NLCR(tty)		_I_FLAG((tty), INLCR)			// 取换行符 NL 转回车符 CR 标志.
#define I_CRNL(tty)		_I_FLAG((tty), ICRNL)			// 取回车符 CR 转换行符 NL 标志.
#define I_NOCR(tty)		_I_FLAG((tty), IGNCR)			// 取忽略回车符 CR 标志.
#define I_IXON(tty)		_I_FLAG((tty), IXON)			// 取输入控制流标志 XON.

// 取 termios 结构输出模式标志集中的一个标志.
#define O_POST(tty)		_O_FLAG((tty), OPOST)			// 取执行输出处理标志.
#define O_NLCR(tty)		_O_FLAG((tty), ONLCR)			// 取换行符 NL 转回车换行符 CR-NL 标志.
#define O_CRNL(tty)		_O_FLAG((tty), OCRNL)			// 取回车符 CR 转换行符 NL 标志.
#define O_NLRET(tty)	_O_FLAG((tty), ONLRET)			// 取换行符 NL 执行回车功能的标志.
#define O_LCUC(tty)		_O_FLAG((tty), OLCUC)			// 取小写转大写字符标志.

// 取 termios 结构控制标志集中波特率. CBAUD 是波特率屏蔽码(0000017). 
#define C_SPEED(tty)	((tty)->termios.c_cflag & CBAUD)
// 判断 tty 终端是否已挂线(hang up), 即其传输波特率是否为 B0(0). 
#define C_HUP(tty)	(C_SPEED((tty)) == B0)

// 取最小值宏.
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

// tty_struct - tty 终端, tty_queue - tty 缓冲队列.
// 下面定义 tty 终端使用的缓冲队列结构数组 tty_queues 和 tty 终端表结构数组 tty_table.
// QUEUES 是 tty 终端使用的缓冲队列最大数量. 伪终端分主从两种(master 和 slave). 
// 每个 tty 终端(tty_struct)使用 3 个 tty 缓冲队列(tty_queue), 它们分别是:
// read_queue: 		用于缓冲键盘或串行输入的读队列.
// write_queue: 	用于缓冲屏幕或串行输出的写队列.
// secondary: 		用于保存规范模式字符的辅助缓冲队列.
#define QUEUES	(3 * (MAX_CONSOLES + NR_SERIALS + 2 * NR_PTYS))		// 共 54 项(3 * (8 + 2 + 2 * 4) = 3 * 18 = 54).
static struct tty_queue tty_queues[QUEUES];			// tty 缓冲队列数组.
struct tty_struct tty_table[256];					// tty 表结构数组. 用于保存每个终端设备的信息，每项对应系统的一个终端设备.

// 下面设定各种类型的 tty 终端所使用缓冲队列结构在 tty_queues[] 数组中的起始项位置.
// 8 个虚拟控制台终端占用 tty_queues[] 数组开头 24 项(3 * MAX_CONSOLES)(0 -- 23).
// 2 个串行终端(rs_queues)占用随后的 6 项(3 * NR_SERIALS)(24 -- 29).
// 4 个主伪终端(mpty_queues)占用随后的 12 项(3 * NR_PTYS)(30 -- 41).
// 4 个从伪终端(spty_queues)占用随后的 12 项(3 * NR_PTYS)(42 -- 53).
#define con_queues tty_queues
#define rs_queues ((3 * MAX_CONSOLES) + tty_queues)
#define mpty_queues ((3 * (MAX_CONSOLES + NR_SERIALS)) + tty_queues)
#define spty_queues ((3 * (MAX_CONSOLES + NR_SERIALS + NR_PTYS)) + tty_queues)

// 下面设定各种类型 tty 终端所使用的 tty 结构在 tty_table[] 数组中的起始项位置.
// 8 个虚拟控制台终端可用 tty_table[] 数组开头 64 项(0 -- 63);
// 2 个串行终端使用随后的 2 项(64 -- 65);
// 4 个主伪终端使用从 128 开始的项, 最多 64 项(128 -- 191).
// 4 个从伪终端使用从 192 开始的项, 最多 64 项(192 -- 255).
#define con_table tty_table							// 定义控制台终端 tty 表符号常数.
#define rs_table (64 + tty_table)					// 串行终端 tty 表.
#define mpty_table (128 + tty_table)				// 主伪终端 tty 表.
#define spty_table (192 + tty_table)				// 从伪终端 tty 表.

int fg_console = 0;									// 当前前台控制台号(范围 0--7).

/*
 * these are the tables used by the machine code handlers.
 * you can implement virtual consoles.
 */
/*
 * 下面是汇编程序中使用的缓冲队列结构地址表. 通过修改这个表, 你可以实现虚拟控制台.
 */
// tty 读写缓冲队列结构地址表. 供 rs_io.s 程序使用, 用于取得读写缓冲队列结构的地址.
struct tty_queue * table_list[] = {
	con_queues + 0, con_queues + 1,					// 前台控制台读, 写队列结构地址.
	rs_queues + 0, rs_queues + 1,					// 串行终端 1 读, 写队列结构地址.
	rs_queues + 3, rs_queues + 4					// 串行终端 2 读, 写队列结构地址.
};

// 改变前台控制台.
// 将前台控制台设定为指定的虚拟控制台.
// 参数: new_console - 指定的新控制台号.
void change_console(unsigned int new_console) {
	// 如果参数指定的控制台已经在前台或者参数无效, 则退出. 否则设置当前前台控制台号, 
	// 同时更新 table_list[] 中的前台控制台读/写队列结构地址. 最后更新当前前台控制台屏幕.
	if (new_console == fg_console || new_console >= NR_CONSOLES) {
		return;
	}
	fg_console = new_console;
	table_list[0] = con_queues + 0 + fg_console * 3;
	table_list[1] = con_queues + 1 + fg_console * 3;
	update_screen();
}

// 如果队列缓冲区空则让进程进入可中断睡眠状态.
// 参数: queue - 指定队列的指针.
// 进程在取队列缓冲区中字符之前需要调用此函数加以验证. 如果当前进程没有信号要处理, 
// 并且指定的队列缓冲区空, 则让进程进入可中断睡眠状态, 并让队列的进程等待指针指向该进程.
static void sleep_if_empty(struct tty_queue * queue) {
	cli();
	while (!(current->signal & ~current->blocked) && EMPTY(queue)) {
		interruptible_sleep_on(&queue->proc_list);
	}
	sti();
}

// 若队列缓冲区满则让进程进入可中断的睡眠状态.
// 参数: queue - 指定队列的指针.
// 进程在往队列缓冲区中写入字符之前需要调用此函数判断队列的情况.
static void sleep_if_full(struct tty_queue * queue) {
	// 如果队列缓冲区不满则返回退出. 否则若进程没有信号需要处理, 并且队列缓冲区中空闲剩余区长度 < 128, 
	// 则让进程进入可中断息状态, 并让该队列的进程等待指针指向该进程.
	if (!FULL(queue)) return;
	cli();
	while (!(current->signal & ~current->blocked) && LEFT(queue) < 128) {
		interruptible_sleep_on(&queue->proc_list);
	}
	sti();
}

// 等待按键.
// 如果前台控制台读队列缓冲区空, 则让进程进入可中断睡眠状态.
void wait_for_keypress(void) {
	sleep_if_empty(tty_table[fg_console].secondary);
}

// 复制成规范模式字符序列
// 根据终端 termios 结构中设置的各种标志, 
// 将指定 tty 同读队列缓冲区中的字符复制转换成规范模式(熟模式)字符并存放在辅助队列(规范模式队列)中.
// 参数: tty - 指定终端的 tty 结构指针.
void copy_to_cooked(struct tty_struct * tty) {
	signed char c;

	// 首先检查当前终端 tty 结构中缓冲队列指针是否有效. 如果三个队列指针都是 NULL, 则说明内核 tty 初始化函数有问题.
	if (!(tty->read_q || tty->write_q || tty->secondary)) {
		printk("copy_to_cooked: missing queues\n\r");
		return;
	}
	// 否则我们根据终端 termios 结构中的输入和本地标志, 
	// 对从 tty 读队列缓冲区中取出的每个字符进行适当的处理, 然后放入辅助队列 secondary 中. 
	// 在下面循环体中, 如果此时读队列缓冲区已经取空或都辅助队列缓冲区已经放满字符, 就退出循环体. 
	// 否则程序就从读队列缓冲区尾指针处取一字符, 并把尾指针前移一个字符位置. 然后根据该字符代码值进行处理.
	// 另外, 如果定义了 _POSIX_VDISABLE(\0), 那么在对字符处理过程中, 
	// 若字符代码值等于 _POSIX_VDISABLE 的值时, 表示禁止使用相应特殊控制字符的功能.
	while (1) {
		// 如果 tty 对应的读队列为空则直接立刻中断循环.
		if (EMPTY(tty->read_q)) break;
		// 如果 tty 对应的第三个队列为空则直接立刻中断循环.
		if (FULL(tty->secondary)) break;
		GETCH(tty->read_q, c);								// 取一字符到 c, 并前移尾指针.
		// 如果该字符是回车符 CR(13), 那么若回车换行转换行 CRNL 置位, 则将字符转换为换行符 NL(10). 
		// 否则如果忽略回车标志 NOCR 置位, 则忽略该字符, 继续处理其他字符.
		if (c == 13) {
			if (I_CRNL(tty)) {
				c = 10;
			} else if (I_NOCR(tty)) {
				continue;
			}
		// 如果字符是换行符 NL(10), 换行转回车标志 NLCR 置位, 则将其转换为回车符 CR(13).
		} else if (c == 10 && I_NLCR(tty)) {
			c = 13;
		}
		// 如果大写转小写输入标志 UCLC 置位, 则将该字符转换为小写字符.
		if (I_UCLC(tty)) {
			c = tolower(c);
		}
		// 如果本地模式标志集中规范模式标志 CANON 已置位, 则对读取的字符进行以下处理. 
		// 首先, 如果该字符是终止控制字符 KILL(^U), 则对已输入的当前行执行删除处理. 
		// 删除一行字符的循环过程如是: 如果 tty 辅助队列不空, 并且取出的辅助队列中最后一个字符不是换行符 NL(10), 
		// 并且该字符不是文件结束字符(^D), 则循环执行下列代码:
		// 如果本了回显标志 ECHO 置位, 那么: 若字符是控制字符(值 < 32), 则往 tty 写队列放入擦除控制字符 ERASE(^H). 
		// 然后再放入一个擦除字符 ERASE, 并且调用该 tty 写函数, 把写队列中的所有字符输出到终端屏幕上. 
		// 另外, 因为控制字符在放入写队列时需要用 2 个字节表示(例如 ^V), 因此要求特别对控制字符多放入一个 ERASE. 
		// 最后将 tty 辅助队列头指针后退 1 字节. 另外, 如果了 _POSIZ_VDISABLE(\0), 那么在对字符修理过程中, 
		// 若字符代码值等于 _POSIX_VDISABLE 的值时, 表示禁止使用相应特殊控制字符的功能.
		if (L_CANON(tty)) {
			if ((KILL_CHAR(tty) != _POSIX_VDISABLE) && (c == KILL_CHAR(tty))) {
				/* deal with killing the input line */
				while(!(EMPTY(tty->secondary) || (c = LAST(tty->secondary)) == 10 ||
				        ((EOF_CHAR(tty) != _POSIX_VDISABLE) && (c == EOF_CHAR(tty))))) {
					if (L_ECHO(tty)) {						// 若本地回显标志置位.
						if (c < 32) {							// 控制字符要删 2 字节.
							PUTCH(127, tty->write_q);
						}
						PUTCH(127, tty->write_q);
						tty->write(tty);
					}
					DEC(tty->secondary->head);
				}
				continue;									// 继续读取读队列中字符进行处理.
			}
			// 如果该字符是删除控制字符 ERASE(^H), 那么: 如果 tty 的辅助队列为空, 或者其最后一个字符是换行符 NL(10), 
			// 或者是文件结束符, 则继续处理其他字符. 如果本地回显标志 ECHO 置位, 那么: 若字符是控制字符(值 < 32), 
			// 则往 tty 的写队列中放入擦除字符 ERASE. 再放入一个擦除字符 ERASE, 并且调用该 tty 的写函数. 
			// 最后将 tty 辅助队列头指针后退 1 字节, 继续处理其他字符. 同样地, 如果定义了 _POSIX_VDISABLE(\0), 
			// 那么在对字符处理过程中, 若字符代码值等于 _POSIX_VDISABLE 的值时, 表示禁止使用相应特殊控制字符的功能.
			if ((ERASE_CHAR(tty) != _POSIX_VDISABLE) && (c == ERASE_CHAR(tty))) {
				if (EMPTY(tty->secondary) || (c = LAST(tty->secondary)) == 10 || ((EOF_CHAR(tty) != _POSIX_VDISABLE) && (c == EOF_CHAR(tty)))) {
					continue;
				}
				if (L_ECHO(tty)) {							// 若本地回显标志置位.
					if (c < 32) {
						PUTCH(127, tty->write_q);
					}
					PUTCH(127, tty->write_q);
					tty->write(tty);
				}
				DEC(tty->secondary->head);
				continue;
			}
        }
		// 如果设置了 IXON 标志, 则使终端停止/开始输出控制字符起作用. 
		// 如果没有设置此标志, 那么停止和开始字符将被作为一般字符供进程读取. 
		// 在这段代码中, 如果读取的字符是停止字符 STOP(^S), 则置 tty 停止标志, 让 tty 暂停输出. 
		// 同时丢弃该特殊控制字符(不放入辅助队列中), 并继续处理其他字符. 
		// 如果字符是开始字符 START(^Q), 则复位 tty 停止标志, 恢复 tty 输出. 
		// 同时丢弃该控制字符, 并继续处理其他字符. 
		// 对于控制台来说, 这里的 tty->write() 是 console.c 中的 con_write() 函数. 
		// 因此对于伪终端也是由于设置了终端 stopped 标志而会暂停写操作(chr_drv/console.c). 
		// 对于伪终端也是由于设置了终端 stopped 标志而会暂停写操作(chr_drv/pty.c).
		// 对于串行终端, 也应该在发送终端过程中根据终端 stopped 标志暂停发送, 但本版未实现.
		if (I_IXON(tty)) {
			if ((STOP_CHAR(tty) != _POSIX_VDISABLE) && (c==STOP_CHAR(tty))) {
				tty->stopped = 1;
				tty->write(tty);
				continue;
			}
			if ((START_CHAR(tty) != _POSIX_VDISABLE) && (c==START_CHAR(tty))) {
				tty->stopped = 0;
				tty->write(tty);
				continue;
			}
        }
		// 若输入模式标志集中 ISIG 标志置位, 表示终端键盘可以产生信号, 
		// 则在收到控制字符 INTR, QUIT, SUSP 或 DSUSP 时, 需要为进程产生相应的信号. 
		// 如果该字符是键盘中断符(^C), 则向当前进程之进程组中所有进程发送键盘中断信号 SIGINT, 并继续处理下一字符. 
		// 如果该字符是退出符(^\), 则向当前进程之进程组中所有进程发送键盘退出信号 SIGQUIT, 并继续处理下一字符. 
		// 如果字符是暂停符(^Z), 则向当前进程发送暂停信息 SIGTSTP. 
		// 同样, 若定义了 _POSIX_VDISABLE(\0), 那么在对字符处理过程中, 
		// 若字符代码值等于 _POSIX_VDISABLE 的值时, 表示禁止使用相应特殊控制字符的功能.
		if (L_ISIG(tty)) {
			if ((INTR_CHAR(tty) != _POSIX_VDISABLE) && (c==INTR_CHAR(tty))) {
				kill_pg(tty->pgrp, SIGINT, 1);
				continue;
			}
			if ((QUIT_CHAR(tty) != _POSIX_VDISABLE) && (c==QUIT_CHAR(tty))) {
				kill_pg(tty->pgrp, SIGQUIT, 1);
				continue;
			}
			if ((SUSPEND_CHAR(tty) != _POSIX_VDISABLE) && (c == SUSPEND_CHAR(tty))) {
				if (!is_orphaned_pgrp(tty->pgrp)) {				// 判断一个进程组是否孤儿进程.
					kill_pg(tty->pgrp, SIGTSTP, 1);
				}
				continue;
			}
		}
		// 如果该字符是换行符 NL(10), 或者是文件结束符 EOF(4,^D), 
		// 表示一行字符已处理完, 则把辅助缓冲队列中当前含有字符行数值 secondar. 
		// data 增 1. 如果在函数 tty_read() 中取走一行字符, 该值即会减 1.
		if (c == 10 || (EOF_CHAR(tty) != _POSIX_VDISABLE && c == EOF_CHAR(tty))) {
			tty->secondary->data++;
		}
		// 如果本地模式标志中回显标志 ECHO 在置位状态, 那么, 如果字符是换行符 NL(10), 
		// 则将换行符 NL(10) 和回车符(13) 放入 tty 写队列缓冲区中; 
		// 如果字符是控制字符(值 < 32)并且回显控制字符标志 ECHOCTL 置位, 
		// 则将字符 '^' 和字符 c + 64 放入 tty 写队列中(也即会显示 ^C, ^H 等); 
		// 否则将该字符直接放入 tty 写缓冲队列中. 最后调用该 tty 写操作函数.
		if (L_ECHO(tty)) {
			if (c == 10) {
				PUTCH(10, tty->write_q);
				PUTCH(13, tty->write_q);
			} else if (c < 32) {
				if (L_ECHOCTL(tty)) {
					PUTCH('^', tty->write_q);
					PUTCH(c + 64, tty->write_q);
				}
			} else {
				PUTCH(c, tty->write_q);
			}
			tty->write(tty);
		}
		// 每一次循环末将处理过的字符放入辅助队列中.
		PUTCH(c, tty->secondary);
    }
	// 在退出循环体后唤醒等待该辅助缓冲队列的进程(如果有的话).
	wake_up(&tty->secondary->proc_list);
}

/*
 * Called when we need to send a SIGTTIN or SIGTTOU to our process
 * group
 *
 * We only request that a system call be restarted if there was if the
 * default signal handler is being used.  The reason for this is that if
 * a job is catching SIGTTIN or SIGTTOU, the signal handler may not want
 * the system call to be restarted blindly.  If there is no way to reset the
 * terminal pgrp back to the current pgrp (perhaps because the controlling
 * tty has been released on logout), we don't want to be in an infinite loop
 * while restarting the system call, and have it always generate a SIGTTIN
 * or SIGTTOU.  The default signal handler will cause the process to stop
 * thus avoiding the infinite loop problem.  Presumably the job-control
 * cognizant parent will fix things up before continuging its child process.
 */
/*
 * 当需要发送信号 SIGTTIN 或 SIGTTOU 到我们进程组中所有进程时就会调用该函数. 
 *
 * 在进程使用默认信号处理句柄的情况下, 我们仅要求一个系统调用被重新启动, 如果有系统调用因本信号而被中断. 
 * 这样做的有因是, 如果一个作业正在捕获 SIGTTIN 或 SIGTTOU 信号, 那么相应信号句柄并不会希望系统调用被盲目地重新启动. 
 * 如果没有其他方法把终端的 pgrp 复位到当前 pgrp(例如可能由于在 logout 时控制终端已被释放), 
 * 那么我们并不希望在重新启动系统调用时掉入一个无限循环中, 并且总是产生 SIGTTIN 或 SIGTTOU 信号. 
 * 默认的信号句柄会使得进程停止, 因而可以避免无限循环问题. 这里假设可识别作业控制的父进程会在继续执行其子进程之前把问题搞定. 
 */
// 向使用终端的进程组中所有进程发送信号. 
// 在后台进程组中的一个进程访问控制终端时, 该函数用于向后台进程组中的所有进程发送 SIGTTIN 或 SIGTTOU 信号. 
// 无论后台进程组中的进程是否已经阻塞或忽略掉了这两个信号, 当前进程都将立刻退出读写操作而返回. 
int tty_signal(int sig, struct tty_struct *tty) {
	// 我们不希望停止一个孤儿进程组的进程(参见文件 kernel/exit.c 的说明). 
	// 因此如果当前进程组是孤儿进程组, 就出错返回. 否则就向当前进程组所有进程发送指定信号. 
	if (is_orphaned_pgrp(current->pgrp)) {
		return -EIO;									/* don't stop an orphaned pgrp */
	}
	(void) kill_pg(current->pgrp, sig, 1);            	// 发送信号 sig. 
	// 如果这个信号被当前进程阻塞(屏蔽), 或者被当前进程忽略掉, 则出错返回. 
	// 否则, 如果当前进程的对信号 sig 设置了新的处理句柄, 那么就返回我们可被中断的信息. 
	// 否则就返回在系统调用重新启动后可以继续执行的信息. 
	if ((current->blocked & (1 << (sig - 1))) || ((int) current->sigaction[sig - 1].sa_handler == 1)) {
		return -EIO;		/* Our signal will be ignored */
	} else if (current->sigaction[sig-1].sa_handler) {
		return -EINTR;		/* We _will_ be interrupted :-) */
	} else {
		return -ERESTARTSYS;	/* We _will_ be interrupted :-) */
	}							/* (but restart after we continue) */
}

// tty 读函数. 
// 从终端辅助缓冲队列读取指定数量的字符, 放到用户指定的缓冲区中. 
// 参数: channel - 子设备号; buf - 用户缓冲区指针; nr - 欲读字节数. 
int tty_read(unsigned channel, char * buf, int nr) {
	struct tty_struct * tty;
	struct tty_struct * other_tty = NULL;
	char c, * b = buf;
	int minimum, time;

	// 首先判断参数有效性并取终端的 tty 结构指针. 
	// 如果 tty 终端的三个缓冲队列指针都是 NULL, 则返回 EIO 出错信息. 
	// 如果 tty 终端是一个伪终端, 则再取得另一个对应伪终端的 tty 结构 other_tty. 
	if (channel > 255) {
		return -EIO;
	}
	tty = TTY_TABLE(channel);
	if (!(tty->write_q || tty->read_q || tty->secondary)) {
		return -EIO;
	}
	// 如果当前进程使用的是这里正在处理的 tty 终端, 但该终端的进程组号却与当前进程组号不同, 
	// 表示当前进程是后台进程组中的一个进程, 即进程不在前台, 于是我们要停止当前进程组的所有进程. 
	// 因此这里就需要向当前进程组发送 SIGTTIN 信号, 并返回等待成为前台进程组后再执行读操作. 
	if ((current->tty == channel) && (tty->pgrp != current->pgrp)) {
		return(tty_signal(SIGTTIN, tty));
	}
	// 如果当前终端是伪终端, 那么对应的另一个伪终端就是 other_tty. 
	// 若这里 tty 是主伪终端, 那么 other_tty 就是对应的从伪终端, 反之亦然. 
	if (channel & 0x80) {
		other_tty = tty_table + (channel ^ 0x40);
	}
	// 然后根据 VTIME 和 VMIN 对应的控制字符数组值设置读字符操作超时定时值 time 和最少需要读取的字符个数 minimum. 
	// 在非规范模式下, 这两个是超时定时值. 
	// VMIN 表示为了满足读操作而需要读取的最少字符个数. VTIME 是一个 1/10 秒计数计时值. 
	time = 10L * tty->termios.c_cc[VTIME];            				// 设置读操作超时定时值. 
	minimum = tty->termios.c_cc[VMIN];              				// 最少需要读取的字符个数. 
	// 如果 tty 终端处于规范模式, 则设置最小要读取字符数 minimum 等于进程欲读字符数 nr. 
	// 同时把进程读取 nr 字符的超时时间值设置为极大值(不会超时). 否则说明终端处于非规范模式下, 
	// 若此时设置了最少读取字符数 minimum, 则先临时设置进程读超时定时值为无限大, 以让进程先读取辅助队列中已有字符. 
	// 如果读到的字符数不足 minimum 的话, 后面代码会根据指定的超时值 time 来设置进程的读超时值 timeout, 并会等待读取其余字符. 
	// 若此时没有设置最少读取字符数 minimum(为 0), 则将其设置为进程欲读字符数 nr, 并且如果设置了超时定时值 time 的话, 
	// 就把进程读字符超时定时值 timeout 设置为系统当前时间值 + 指定的超时值 time, 同时复位 time. 
	// 另外, 如果以上设置的最少读取字符数 minimum 大于进程欲读取的字符数 nr, 则让 minimum = nr. 
	// 即对于规范模式下的读取操作, 它不受 VTIME 和 VMIN 对应控制字符值的约束和控制, 它们仅在非规范模式(生模式)操作中起作用. 
	if (L_CANON(tty)) {
		minimum = nr;
		current->timeout = 0xffffffff;
		time = 0;
	} else if (minimum) {
		current->timeout = 0xffffffff;
	} else {
		minimum = nr;
		if (time) {
			current->timeout = time + jiffies;
		}
		time = 0;
	}
	if (minimum > nr) {
		minimum = nr;           									// 最多读取要求的字符数. 
	}
	// 现在我们开始从辅助队列中循环取出字符并放到用户缓冲区 buf 中. 当欲读的字节数大于 0, 则执行以下循环操作. 
	// 在循环过程中如果当前终端是伪终端, 那么我们就执行其对应的另一个伪终端的写操作函数, 
	// 让另一个伪终端把字符写入当前伪终端辅助队列缓冲区中. 
	// 即让另一终端把写队列缓冲区中字符复制到当前伪终端读队列缓冲区中, 并经行规则函数转换后放入当前伪终端辅助队列中. 
	while (nr > 0) {
		if (other_tty) {
			other_tty->write(other_tty);
		}
		// 如果 tty 辅助缓冲队列为空, 或者设置了规范模式标志并且 tty 读队列缓冲区未满, 并且辅助队列中字符行数为 0, 
		// 那么, 如果没有设置过进程读字符超时值(为 0), 或者当前进程目前收到信号, 就先退出循环体. 
		// 否则如果本终端是一个从伪终端, 并且其对应的主伪终端已经挂断, 那么我们也退出循环体. 
		// 如果不是以上这两种情况, 我们就让当前进程进入可中断睡眠状态, 返回后继续处理. 
		// 由于规范模式时内核以行为单位为用户提供数据, 因此在该模式下辅助队列中必须至少有一行字符可供取胜, 
		// 即 secondary.data 起码是 1 才行. 
		cli();
		if (EMPTY(tty->secondary) || (L_CANON(tty) && !FULL(tty->read_q) && !tty->secondary->data)) {
			if (!current->timeout || (current->signal & ~current->blocked)) {
			  	sti();
				break;
			}
			if (IS_A_PTY_SLAVE(channel) && C_HUP(other_tty)) {
				break;
			}
			interruptible_sleep_on(&tty->secondary->proc_list);
			sti();
			continue;
		}
		sti();
		// 下面开始正式执行取字符操作. 需读字符数 nr 依次递减, 直到 nr = 0或者辅助缓冲队列为空. 
		// 在这个循环过程中, 首先取辅助缓冲队列字符 c, 并且把缓冲队列尾指针 tail 向右移动一个字符位置. 
		// 如果所取字符是文件结束符(^D)或者是换行符 NL(10), 则把辅助缓冲队列中含有字符行数值减 1. 
		// 如果该字符是文件结束符(^D)并且规范模式标志成置位状态, 则中断本循环, 
		// 否则说明现在还没有遇到文件结束符或者正处于原始(非规范)模式. 
		// 在这种模式中用户以字符流作为读取对象, 也不识别其中的控制字符(如文件结束符). 
		// 于是将字符直接放入用户数据缓冲区 buf 中, 并把欲读字符数减 1.
		// 此时如果欲读字符数已为 0 则中断循环. 
		// 另外, 如果终端处于规范模式并且读取的字符是换行符 NL(10), 则也退出循环. 
		// 除此之外, 只要还没有取完欲读字符数 nr 并且辅助队列不为空, 就继续取队列中的字符. 
		do {
			GETCH(tty->secondary, c);
			if ((EOF_CHAR(tty) != _POSIX_VDISABLE && c == EOF_CHAR(tty)) || c == 10) {
				tty->secondary->data--;
			}
			if ((EOF_CHAR(tty) != _POSIX_VDISABLE && c == EOF_CHAR(tty)) && L_CANON(tty)) {
				break;
			} else {
				put_fs_byte(c, b++);
				if (!--nr) break;
			}
			if (c == 10 && L_CANON(tty)) break;
		} while (nr > 0 && !EMPTY(tty->secondary));
		// 执行到此, 那么如果 tty 终端处于规范模式下, 说明我们可能读到了换行符或者遇到了文件结束符. 
		// 如果是处于非规范模式下, 那么说明我们已经读取了 nr 个字符, 或者辅助队列已经被取空了. 
		// 于是我们首先唤醒等待队列的进程, 然后看看是否设置过超时定时值 time. 
		// 如果超时定时值 time 不为 0, 我们就要求等待一定的时间让其他进程可以把字符写入读队列中. 
		// 于是设置进程读超时定时值为系统当前时间 jiffies + 读超时值 time. 
		// 当然, 如果终端处于规范模式, 或者已经读取 nr 个字符, 我们就可以直接退出这个大循环了. 
		wake_up(&tty->read_q->proc_list);
		if (time) {
			current->timeout = time + jiffies;
		}
		if (L_CANON(tty) || b - buf >= minimum) break;
    }
	// 此时读取 tty 字符循环操作结束, 因此复位进程的读取超时定时值 timeout. 
	// 如果此时当前进程已收到信号并且还没有读取到任何字符则以重新启动系统调用号 "-ERESTARTSYS" 返回. 
	// 否则就返回已读取的字符数(b - buf). 
	current->timeout = 0;
	if ((current->signal & ~current->blocked) && !(b - buf)) {
		return -ERESTARTSYS;
	}
	return (b - buf);
}

// tty 写函数.
// 把用户缓冲区中的字符放入 tty 写队列缓冲区中.
// 参数: channel - 子设备号; buf - 缓冲区指针; nr - 写字节数.
// 返回已写字节数.
int tty_write(unsigned channel, char * buf, int nr) {
	static int cr_flag = 0;
	struct tty_struct * tty;
	char c, * b = buf;

	// 首先判断参数有效性并取终端的 tty 结构指针. 
	// 如果 tty 终端的三个缓冲队列指针都是 NULL, 则返回 EIO 出错信息.
	if (channel > 255) {
		return -EIO;
	}
	tty = TTY_TABLE(channel);
	if (!(tty->write_q || tty->read_q || tty->secondary)) {
		return -EIO;
	}
	// 如果若终端本地模式标志集中设置了 TOSTOP, 表示后台进程输出时需要发送信号 SIGTTOU. 
	// 如果当前进程使用的是这里正在处理的 tty 终端, 但该终端的进程组号却与当前进程组号不同, 
	// 即表示当前进程是后台进程组中的一个进程, 即进程不在前台, 于是我们要停止当前进程组的所有进程. 
	// 因此这里就需要向当前进程组发送 SIGTTOU 信号, 并返回等待成为前台进程组后再执行写操作.
	if (L_TOSTOP(tty) && (current->tty == channel) && (tty->pgrp != current->pgrp)) {
		return(tty_signal(SIGTTOU, tty));
	}
	// 现在我们开始从用户缓冲区 buf 中循环取出字符并放到写队列缓冲区中. 
	// 当欲写字节数大于 0, 则执行以下循环操作. 
	// 在循环过程中, 如果此时 tty 写队列已满, 则当前进程进入可中断睡眠状态. 
	// 如果当前进程有信号要处理, 则退出循环体.
	while (nr > 0) {
		sleep_if_full(tty->write_q);
		if (current->signal & ~current->blocked) break;

		// 当要写的字符数 nr 还大于 0 并且 tty 写队列缓冲区不满, 则循环执行以下操作. 首先从用户缓冲区中取 1 字节.
		while (nr > 0 && !FULL(tty->write_q)) {
			c = get_fs_byte(b);
			// 如果终端输出模式标志集中的执行输出处理标志 OPOST 置位, 则执行对字符的后处理操作.
			if (O_POST(tty)) {
				// 如果该字符是回车符 '\r'(CR, 13) 并且回车符转换行标志 OCRNL 置位, 则将该字符换成行符 '\n'(NL, 10);
				if (c == '\r' && O_CRNL(tty)) {
					c = '\n';
				// 如果该字符是换行符 '\n'(NL, 10)并且换行转回车功能标志 ONLRET 置位的话, 则将该字符换成回车符 '\r'(CR, 13).
				} else if (c == '\n' && O_NLRET(tty)) {
					c = '\r';
				}
				// 如果该字符是换行符 '\n' 并且回车标志 cr_flag 没有置位, 但换行转回车 - 换行标志 ONLCR 置位的话, 
				// 则将 cr_flag 标志置位, 并将一回车符放入写队列中. 然后继续处理下一个字符.
				if (c == '\n' && !cr_flag && O_NLCR(tty)) {
					cr_flag = 1;
					PUTCH(13, tty->write_q);
					continue;
				}
				// 如果小写转大写标志 OLCUC 置位的话, 就将该字符转成大写字符.
				if (O_LCUC(tty)) {
					c = toupper(c);									// 小写转成大写字符.
				}
			}
			// 接着把用户数据缓冲指针 b 前移 1 字节; 欲写字节数减 1 字节; 
			// 复位 cr_flag 标志, 并将该字节放入 tty 写队列中.
			b++; nr--;
			cr_flag = 0;
			PUTCH(c, tty->write_q);
		}
		// 若要求的字符全部写完, 或者写队列已满, 则程序退出循环. 
		// 此时会调用对应 tty 写函数, 把写队列缓冲区中的字符显示在控制台屏幕上, 或者通过串行端口发送出去. 
		// 如果当前处理的 tty 是控制台终端, 那么 tty->write() 调用的是 con_write(); 
		// 如果 tty 是串行终端, 则 tty->write() 调用的是 rs_write() 函数. 
		// 若还有字节要写, 则等待写队列中字符取走. 所以这里调用调度程序, 先去执行其他任务.
		tty->write(tty);
		if (nr > 0) {
			schedule();
		}
	}
	return (b - buf);												// 最后返回写入的字节数.
}

/*
 * Jeh, sometimes I really like the 386.
 * This routine is called from an interrupt,
 * and there should be absolutely no problem
 * with sleeping even in an interrupt (I hope).
 * Of course, if somebody proves me wrong, I'll
 * hate intel for all time :-). We'll have to
 * be careful and see to reinstating the interrupt
 * chips before calling this, though.
 *
 * I don't think we sleep here under normal circumstances
 * anyway, which is good, as the task sleeping might be
 * totally innocent.
 */
/*
 * 呵, 有时我真的是很喜欢 386. 
 * 该子程序被从一个中断处理程序中调用, 并且即使在中断处理程序中睡眠也应该绝对没有问题(我希望如此). 
 * 当然, 如果有人证明我是错的, 那么我将憎恨 Intel 一辈子.
 *
 * 我不认为在通常环境会处在这里睡眠, 这样很好, 因为任务睡眠是完全任意的.
 */
// tty 中断处理调用函数 - 字符规范模式处理.
// 参数: tty - 指定的 tty 终端号.
// 将指定 tty 终端队列缓冲区中的字符复制或转换成规范(熟)模式字符并存放在辅助队列中. 
// 该函数会在串口读字符中断(rs_io.s)和键盘中断(kerboard.S)中被调用.
void do_tty_interrupt(int tty) {
	copy_to_cooked(TTY_TABLE(tty));
}

//字符设备初始化函数. 空, 为以后扩展做准备.
void chr_dev_init(void) {
}

// tty 终端初始化函数
// 初始化所有终端缓冲队列, 初始化串口终端和控制台终端.
void tty_init(void) {
	int i;

	// 首先初始化所有终端的缓冲队列结构(tty_queue), 设置初值. 
	// 对于串行终端(rs_queues)的读/写缓冲队列, 将它们的 data 字段设置为串行端口基地址值. 
	// 串口 1 是 0x3f8, 串口 2 是 0x2f8. 然后先初步设置所有终端的 tty 结构.
	// 其中特殊字符数组 c_cc[] 设置的初值定义在 include/linux/tty.h 文件中.
	for (i = 0; i < QUEUES; i++) { 							// QUEUES = 54
		tty_queues[i] = (struct tty_queue) {0, 0, 0, 0, ""};
	}
	rs_queues[0] = (struct tty_queue) {0x3f8, 0, 0, 0, ""};
	rs_queues[1] = (struct tty_queue) {0x3f8, 0, 0, 0, ""};
	rs_queues[3] = (struct tty_queue) {0x2f8, 0, 0, 0, ""};
	rs_queues[4] = (struct tty_queue) {0x2f8, 0, 0, 0, ""};
	// 初步设置所有终端的 tty 结构体
	for (i = 0; i < 256; i++) {
		tty_table[i] =  (struct tty_struct) {
		 	{0, 0, 0, 0, 0, INIT_C_CC}, 					// termios: 终端 io 属性和控制字符数据结构
			0, 0, 0, 										// pgrp(所属进程组), session(会话号), stopped(结束标志), 
			NULL, NULL, NULL, NULL  						// *write(写函数), read_q(读缓冲队列), write_q(写缓冲队列), secondary(辅助缓冲队列)
		};
	}
	// 接着初始化控制台终端(console.c). 
	// 把 con_init() 放在这里, 是因为我们需要根据显示卡类型和显示内存容量来确定系统虚拟控制台的数量 NR_CONSOLES.
	// 该值被用于随后的控制 tty 结构初始化循环中. 
	// 对于控制台的 tty 结构, tty 结构中包含的 termios 结构字段中输入模式标志集被初始化为 ICRNL 标志;
	// 输出模式标志被初始化含有后处理标志 OPOST 和把 NL 转换成 CRNL 的标志 ONLCR; 
	// 本地模式标志集被初始化含有 IXON, ICAON, ECHO, ECHOCTL 和 ECHOKE 标志; 
	// 控制字符数组 c_cc[] 被设置含有初始值 INIT_C_CC.
	// 最后是初始化控制台终端 tty 结构中的读缓冲, 写缓冲和辅助缓冲队列结构, 
	// 它们分别指向 tty 缓冲队列结构数组 tty_table[] 中的相应结构项.
	con_init();
	for (i = 0; i < NR_CONSOLES; i++) {
		con_table[i] = (struct tty_struct) {
		 	{ICRNL,													/* change incoming CR to NL */		/* CR 转 NL */
			OPOST | ONLCR,											/* change outgoing NL to CRNL */	/* NL 转 CRNL */
			0,														// 控制模式标志集
			IXON | ISIG | ICANON | ECHO | ECHOCTL | ECHOKE,			// 本地标志集
			0,														/* console termio */	// 线路规程, 0 -- TTY
			INIT_C_CC},												// 控制字符数组 c_cc[]
			0,														/* initial pgrp */		// 所属初始进程组 pgrp
			0,														/* initial session */	// 初始会话级 session
			0,														/* initial stopped */	// 初始停止标志 stopped
			con_write, 												// 虚拟控制台(console)写函数. 其它的 tty 类型可以对应其它的 write 函数, 
																	// 比如下面串口终端写函数 rs_write().
			con_queues + 0 + i * 3, con_queues + 1 + i * 3, con_queues + 2 + i * 3 			// 分别设置 con_tty 读/写/辅助 缓冲队列
		};
	}
	// 然后初始化串行终端的 tty 结构各字段. 初始化串行终端 tty 结构中的读/写和辅助缓冲队列结构, 
	// 它们分别指向 tty 缓冲队列结构数组 tty_table[] 中相应结构项. 
	for (i = 0; i < NR_SERIALS; i++) {
		rs_table[i] = (struct tty_struct) {
			{0, 													/* no translation */        // 输入模式标志集. 0, 无须转换. 
			0,  													/* no translation */        // 输出模式标志集. 0, 无须转换. 
			B2400 | CS8,                    						// 控制模式标志集. 2400bpx, 8 位数据位. 
			0,                              						// 本地模式标志集. 
			0,                              						// 线路规程, 0 -- TTY. 
			INIT_C_CC},                     						// 控制字符数组. 
			0,                              						// 所属初始进程组. 
			0,                              						// 初始会话组. 
			0,                              						// 初始停止标志. 
			rs_write,                       						// 串口终端写函数. 
			rs_queues + 0 + i * 3, rs_queues + 1 + i * 3, rs_queues + 2 + i * 3
		};
	}
	// 然后再初始化伪终端(pty)使用的 tty 结构. 伪终端是配对使用的, 即一个主(master)伪终端配有一个从(slave)伪终端. 
	// 因此对它们都要进行初始化设置. 在循环中, 我们首先初始化每个主伪终端的 tty 结构, 然后再初始化其对应的从伪终端的 tty 结构. 
	for (i = 0; i < NR_PTYS; i++) {
		mpty_table[i] = (struct tty_struct) {
			{0, 													/* no translation */        // 输入模式标志集. 0, 无须转换. 
			0,  													/* no translation */        // 输出模式标志集. 0, 无须转换. 
			B9600 | CS8,                    						// 控制模式标志集. 9600bpx, 8 位数据位. 
			0,                              						// 本地模式标志集. 
			0,                              						// 线路规程, 0 -- TTY. 
			INIT_C_CC},                    							// 控制字符数组. 
			0,                              						// 所属初始进程组. 
			0,                              						// 初始会话组. 
			0,                              						// 初始停止标志. 
			mpty_write,                     						// 主伪终端写函数. 
			mpty_queues + 0 + i * 3, mpty_queues + 1 + i * 3, mpty_queues + 2 + i * 3
		};
		spty_table[i] = (struct tty_struct) {
			{0, 													/* no translation */        // 输入模式标志集. 0, 无须转换. 
			0,  													/* no translation */        // 输出模式标志集. 0, 无须转换. 
			B9600 | CS8,                    						// 控制模式标志集. 9600bpx, 8 位数据位. 
			IXON | ISIG | ICANON,           						// 本地模式标志集. 
			0,                              						// 线路规程, 0 -- TTY. 
			INIT_C_CC},                    							// 控制字符数组. 
			0,                              						// 所属初始进程组. 
			0,                              						// 初始会话组. 
			0,                              						// 初始停止标志. 
			spty_write,                     						// 从伪终端写函数. 
			spty_queues + 0 + i * 3, spty_queues + 1 + i * 3, spty_queues + 2 + i * 3
		};
	}
	// 最后初始化串行中断处理程序和串行接口 1 和 2(serial.c), 并显示系统含有的虚拟控制台数 NR_CONSOLES 和伪终端数 NR_PTYS. 
	rs_init();
	printk("%d virtual consoles\n\r", NR_CONSOLES);
	printk("%d pty's\n\r", NR_PTYS);
}
