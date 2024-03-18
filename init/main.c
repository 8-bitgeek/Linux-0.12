/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */
// 开启仿真协处理器
#define EM
// 定义宏 "__LIBRARY__" 是为了包括定义在 unistd.h 中的内嵌汇编代码等信息.
#define __LIBRARY__
// *.h 头文件所在的默认目录是 include/, 则在代码中就不必明确指明其位置. 
// 如果不是 UNIX 的标准头文件, 则需要指明所在的目录, 并用双绰号括住. 
// unitd.h 是标准符号常数与类型文件. 其中定义了各种符号常数和类型, 并声明了各种函数. 
// 如果还定义了符号 __LIBRARY__, 则还会包含系统调用号和内嵌汇编代码 syscall0() 等.
#include <unistd.h>
#include <time.h>			// 时间类型头文件. 其中主要定义了 tm 结构和一些有关时间的函数原形.

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
/*
 * 我们需要下面这些内嵌语句 - 从内核空间创建进程将导致没有写时复制(COPY ON WRITE)!!! 直到执行一个 execve 调用.
 * 这对堆栈可能带来问题. 处理方法是在 fork() 调用后不让 main() 使用任何堆栈. 
 * 因此就不能有函数调用 - 这意味着 fork 也要使用内嵌代码, 否则我们在从 fork() 退出时就要使用堆栈了.
 *
 * 实际上只有 pause 和 fork 需要使用内嵌方式, 以保证从 main() 中不会弄乱堆栈, 但是我们同时还定义了其他一些函数.
 */
// Linux 在内核空间创建进程时不使用写时复制技术(Copy on write). 
// main() 在移动到用户模式(到任务 0)后执行内嵌方式的 fork() 和 pause(), 因此可保证不使用任务 0 的用户栈. 
// 在执行 move_to_user_mode() 之后, 本程序 main() 就以任务 0 的身份(大部分代码还是在内核态下)在运行了. 
// 而任务 0 是所有将创建子进程的父进程. 当字创建一个子进程时(init 进程), 由于任务1代码属于内核空间,
// 因此没有使用写时复制功能. 此时任务 0 的用户栈就是任务 1 的用户栈, 即它们共同使用一个栈空间. 
// 因此希望在 main.c 运行在任务 0 的环境下时不要有对堆栈的任何操作, 以免弄乱堆栈. 
// 而在再次执行 fork() 并执行过 execve() 函数后, 被加载程序已不属于内核空间, 因此可以使用写时复制技术了.

// static inline 修饰的函数: 这个函数大部分表现和普通的 static 函数一样, 只不过在调用这种函数的时候, 
// gcc 会在其调用处将其汇编码展开编译而不为这个函数生成独立的汇编码.

// 下面的 _syscall0() 是 unistd.h 中的内嵌宏代码. 以嵌入汇编的形式调用 Linux 的系统调用中断 0x80. 
// 该中断是所有系统调用的入口. 该条语句实际上是 int fork() 创建进程系统调用. 可展开看之就会立刻明白. 
// syscall0 名称中最后的 0 表示无参数, 1 表示 1 个参数.
// __attribute__ 可以设置函数属性, 放于声明的尾部 “;” 之前.
// 函数属性可以帮助开发者把一些特性添加到函数声明中，从而可以使编译器在错误检查方面的功能更强大.
// __attribute__((always_inline)) 表示将函数强制设置为内联函数 int fork(void) __attribute__((always_inline));
// int pause() 系统调用: 暂停进程的执行, 直到收到一个信号.
// int pause(void) __attribute__((always_inline));
// fork() 系统调用函数的定义
_syscall0(int, fork)								// (kernel/sys_call.s)
// pause() 系统调用函数的定义
_syscall0(int, pause)
// int setup(void * BIOS) 系统调用, 仅用于 linux 初始化(仅在这个程序中被调用).
_syscall1(int, setup, void *, BIOS)
// int sync() 系统调用: 更新文件系统.
_syscall0(int, sync)

#include <linux/tty.h>                  			// tty 头文件, 定义了有关 tty_io, 串行通信方面的参数, 常数.
#include <linux/sched.h>							// 调度程序头文件, 定义了任务结构 task_struct, 第 1 个初始任务的数据. 
													// 还有一些以宏的形式定义的有关描述符参数设置和获取的嵌入式汇编函数程序.
// # include <linux/head.h>
#include <asm/system.h>								// 系统头文件. 定义了设置或修改描述符/中断门等的嵌入式汇编宏.
#include <asm/io.h>									//　I/O 头文件. 以宏的嵌入汇编程序形式定义对 I/O 端口操作的函数.

#include <stddef.h>                     			// 标准定义头文件. 定义了 NULL, offsetof(TYPE, MEMBER).
#include <stdarg.h>									// 标准参数头文件. 以宏的形式定义变量参数列表. 
													// 主要说明了一个类型(va_list)和三个宏(va_start, va_arg 和 va_end), vsprintf, vprintf, vfprintf.
#include <unistd.h>
#include <fcntl.h>                      			// 文件控制头文件. 用于文件及其描述符的操作控制常数符号的定义
//#include <sys/types.h>

#include <linux/fs.h>								// 文件系统头文件. 定义文件表结构(file, buffer_head, m_inode 等).
													// 其中有定义: extern int ROOT_DEV.

#include <linux/kernel.h>							// 内核头文件.

#include <string.h>									// 字符串头文件. 主要定义了一些有关内存或字符串操作的嵌入函数.

static char printbuf[1024];							// 静态字符串数组, 用作内核显示信息的缓存.

extern char *strcpy();
extern int vsprintf();								// 送格式化输出到一字符串中(vsprintf.c).
extern void init(void);								// 函数原型, 初始化.
extern void blk_dev_init(void);						// 块设备初始化子程序(blk_drv/ll_rw_blk.c).
extern void chr_dev_init(void);						// 字符设备初始化(chr_drv/tty_io.c).
extern void hd_init(void);							// 硬盘初始化程序(blk_drv/hd.c).
extern void floppy_init(void);						// 软驱初始化程序(blk_drv/floppy.c).
extern void mem_init(long start, long end);			// 内存管理初始化(mm/memory.c).
extern long rd_init(long mem_start, int length);	// 虚拟盘初始化(blk_drv/ramdisk.c).
extern long kernel_mktime(struct tm * tm);			// 计算系统开机启动时间(秒).

// fork 系统调用函数, 该函数作为 static inline 表示内联函数, 主要用来在 TASK-0 里面创建 TASK-1 的时候内联, 
// TASK-0 使用 int 0x80 指令调用内核代码创建 TASK-1 时会发生特权级的变化, 所以不会使用自己的用户堆栈, 
// 而是使用 tss 中指定的内核态堆栈(特权级 CPL[3 -> 0] 变化引起堆栈切换).
// 该内联函数返回值为新进程的 pid(last_pid) 或者 -1(获取不到正确的新进程号时).
static inline long fork_for_process0() {
	long __res;
	__asm__ volatile (
		"int $0x80\n\t"  			/* 调用系统中断 0x80: system_call(kernel/sys_call.s) */
		: "=a" (__res)  			/* 返回值 -> eax(__res) */ // 返回值(新进程的 pid)放入 __res 中.
		: "0" (2));  				/* 输入为系统中断调用号 __NR_name(2) */ // 输入寄存器为 eax(%0) = 2, 
									/* 即调用 sys_call_table[2](include/linux/sys.h) 中的 sys_fork (kernel/sys_call.s) */

	// 从 fork_for_process0() 返回时, cs = 0xf = 0b-0000-1-1-11 ==> LDT 中第一项(代码段), CPL = 3.
	if (__res >= 0)  				/* 如果返回值(新进程的 pid, 存放在 eax 中) >= 0(创建成功), 则直接返回该值. */
		return __res;
	errno = -__res;  				/* 否则置出错号, 并返回 -1 */
	return -1;
}

// 内核专用 sprintf() 函数. 该函数用于产生格式化信息并输出到指定缓冲区 str 中. 参数 '*fmt' 指定输出将采用格式.
static int sprintf(char * str, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsprintf(str, fmt, args);
	va_end(args);
	return i;
}

/*
 * This is set up by the setup-routine at boot-time
 */
/*
 * 以下这些数据是在内核引导期间由 setup.s 程序设置的.
 */
 // 下面三行分别将指定的线性地址强行转换为给定数据类型的指针, 并获取指针所指内容. 
 // 由于内核代码段被映射到从物理地址零开始的地方, 因此这些线性地址正好也是对应的物理地址.
#define EXT_MEM_K (*(unsigned short *)0x90002)						// 获取 0x90002 中保存的 1MB 以后的扩展内存大小(KB).
#define CON_ROWS ((*(unsigned short *)0x9000e) & 0xff)				// 选定的控制台屏幕的行数
#define CON_COLS (((*(unsigned short *)0x9000e) & 0xff00) >> 8)     // 选定的控制台屏幕的列数
#define DRIVE_INFO (*((struct drive_info *)0x90080))				// 硬盘参数表 32 字节内容.
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)					// 根文件系统所在设备号.
#define ORIG_SWAP_DEV (*(unsigned short *)0x901FA)					// 交换文件所在设备号.

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */
// 这段宏读取 CMOS 实时时钟信息. outb_p 和 inb_p 是 include/asm/io.h 中定义的端口输入输出.
#define CMOS_READ(addr) ({ \
	outb_p(0x80 | addr, 0x70); 					/* 0x70 是写地址端口号, 0x80|addr 是要读取的 CMOS 内存地址. */\
	inb_p(0x71); 								/* 0x71 是读数据端口号. */\
})

// 定义宏. 将 BCD 码转换成二进制值. BCD 码利用半个字节(4 位)表示一个 10 进制数, 因此一个字节表示 2 个 10 进制数.
// (val)&15 取 BCD 表示的 10 进制个位数, 而 (val)>>4 取 BCD 表示的 10 进制十位数, 再乘以 10. 因此最后两者相加就是一个字节 BCD 码的实际二进制数值.
#define BCD_TO_BIN(val) ((val) = ((val)&15) + ((val) >> 4) * 10)

// 该函数取 CMOS 实时钟信息作为开机时间, 并保存到全局变量 startup_time(秒)中. 
// 其中调用的函数 kernel_mktime() 用于计算从 1970年1月1日0时 起到开机当日经过的秒数, 作为开机时间.
static void time_init(void)
{
	struct tm time;								// 时间结构 tm 定义在 include/time.h 中
	// CMOS 的访问速度很慢. 为了减小时间误差, 在读取了下面循环中所有数值后, 若此时 CMOS 中秒值了变化, 那么就重新读取所有值. 
	// 这样内核就能把与 CMOS 时间误差控制在 1 秒之内.
	do {
		time.tm_sec = CMOS_READ(0);				// 当前时间秒值(均是 BCD 码值): bcd 值也是逢 10 进 1, 4 位二进制表示一个十进制位
		time.tm_min = CMOS_READ(2);				// 当前分钟值.
		time.tm_hour = CMOS_READ(4);			// 当前小时值.
		time.tm_mday = CMOS_READ(7);			// 一月中的当天日期.
		time.tm_mon = CMOS_READ(8);				// 当前月份(1-12)
		time.tm_year = CMOS_READ(9);			// 当前年份.
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);					// 转换成进进制数值.
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;								// tm_mon 中月份范围是 0~11.
	startup_time = kernel_mktime(&time);		// 计算开机时间(kernel/mktime.c)
}

 // 下面定义一些局部变量.
static long memory_end = 0;						// 机器具有的物理内存容量(字节数).
static long buffer_memory_end = 0;				// 高速缓冲区(用于缓存块设备的数据, 比如硬盘)末端地址.
static long main_memory_start = 0;				// 主内存(将用于分页)开始的位置.
static char term[32];							// 终端设置字符串(环境参数). 在当前控制台显存末端处.

// 读取并执行 /etc/rc 文件时所使用的命令行参数和环境参数.
static char * argv_rc[] = { "/bin/sh", NULL };		// 调用执行程序时参数的字符串数组.
static char * envp_rc[] = { "HOME=/", NULL, NULL };	// 调用执行程序时的环境字符串数组.

// 运行登录 shell 时所使用的命令行参数和环境参数.
static char * argv[] = { "-/bin/sh",NULL };			// 字符 "-" 是传递给 shell 程序 sh 的一个标志. 通过识别该标志, 
													// sh 程序会作为登录 shell 执行. 其执行过程与 shell 提示符下执行 sh 不一样.
static char * envp[] = { "HOME=/usr/root", NULL, NULL };

struct drive_info { char dummy[32]; } drive_info;	// 用于存放硬盘参数表信息.

// 分页机制已经在 head.s 的 setup_paging 中开启. 
// 内核初始化主程序. 初始化结束后将以任务 0(idle 任务即空闲任务)的身份运行.
// 英文注释含义是 "这里确实是 void, 没错. 在 startup 程序(head.s)中就是这样假设的". 参见 head.h 程序代码.
int main(void)										/* This really IS void, no error here. */
{													/* The startup routine assumes (well, ...) this */
#ifdef EM
	// 开启仿真协处理器
	__asm__("movl %cr0,%eax \n\t" \
	        "xorl $6,%eax \n\t" \
	        "movl %eax,%cr0");
#endif
	/*
	 * Interrupts are still disabled. Do necessary setups, then enable them
	 */
	/*
	 * 此时中断仍被禁止着, 做完必要的设置后再将其开启.
	 */
	// 首先保存根文件系统设备和交换文件设备号, 并根据 setup.s 程序中获取的信息设置控制台终端屏幕行, 列数环境变量 TERM, 
	// 并用其设置初始 init 进程中执行 etc/rc 文件和 shell 程序使用的环境变量, 以及复制内存 0x90080 处的硬盘表.
	// 其中 ROOT_DEV 已在前面包含进的 include/linux/fs.h 文件上被声明为 extern_int, 
	// 而 SWAP_DEV 在 include/linux/mm.h 文件内也作了相同声明.
	// 这里 mm.h 文件并没有显式地列在本程序前部, 因为前面包含进的 include/linux/sched.h 文件中已经含有它.
	// ROOT_DEV = 0x301(769): 表示第一个硬盘(0x300)的第一个分区(0x001). SWAP_DEV = 0x304(772): 第一个硬盘的第四个分区.
 	ROOT_DEV = ORIG_ROOT_DEV;										// ROOT_DEV 定义在 fs/super.c; 
 	SWAP_DEV = ORIG_SWAP_DEV;										// SWAP_DEV 定义在 mm/swap.c;
   	sprintf(term, "TERM=con%dx%d", CON_COLS, CON_ROWS);
	envp[1] = term;
	envp_rc[1] = term;
    drive_info = DRIVE_INFO;										// 复制内存 0x90080 处的硬盘参数表(由之前的汇编代码读取并记录).

	// 接着根据机器物理内存容量设置高速缓冲区和主内存的位置和范围.
	// buffer_memory_end  	-> 高速缓存区末端地址(字节数).
	// memory_end 			-> 机器内存容量(内存末端地址).
	// main_memory_start 	-> 主内存开始地址.
	// 高速缓冲区主要用于缓存硬盘或软盘等块设备中的数据(数据块大小为 1KB), 
	// 当一个进程需要硬盘或软盘中的数据时, 系统会首先把数据读取到高速缓冲区内存中.
	// 当有数据需要写到块设备上时, 系统也是将数据先写入到高速缓冲区中, 然后由块设备驱动程序写到对应的设备上.
	// 主内存区是供所有程序可以随时申请和使用的内存区域.
	memory_end = (1 << 20) + (EXT_MEM_K << 10);						// 内存大小 = 1MB + [扩展内存(k) * 1024] 字节.
	memory_end &= 0xfffff000;										// 忽略不到 4KB(1 页)的内存数.
	if (memory_end > 16 * 1024 * 1024)								// 如果内存量超过 16MB, 则按 16MB 计.
		memory_end = 16 * 1024 * 1024;
	// 根据物理内存的大小设置高速缓冲区的末端大小.
	if (memory_end > 12 * 1024 * 1024) 								// 如果 16MB >= 内存 > 12MB, 则设置高速缓冲区末端 = 4MB.
		buffer_memory_end = 4 * 1024 * 1024;
	else if (memory_end > 6 * 1024 * 1024)							// 否则若 12MB >= 内存 > 6MB, 则设置高速缓冲区末端 = 2MB.
		buffer_memory_end = 2 * 1024 * 1024;
	else
		buffer_memory_end = 1 * 1024 * 1024;						// 否则则设置缓冲区末端 = 1MB.
	// 根据高速缓冲区的末端大小设置主内存区的起始地址, 两者相同, 即高速缓冲区末端为主内存起始端.
	main_memory_start = buffer_memory_end;							// 主内存起始位置 == 高速缓冲区末端.
	// 如果在 Makefile 文件中定义了内存虚拟盘符号 RAMDISK, 则初始化虚拟盘. 此时主内存将减少.
	// 参见 kernel/blk_drv/ramdisk.c
#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK * 1024);
#endif
	// 以下是内核进行所有方面的初始化工作.
	mem_init(main_memory_start, memory_end);		// 主内存区初始化. (mm/memory.c) 初始化 mem_map[], 主内存区为 4MB - mem_end. 一页大小为 4KB.
	trap_init();                              		// 陷阱门(硬件中断向量)初始化. (kernel/traps.c)
	blk_dev_init();									// 块设备初始化. (blk_drv/ll_rw_blk.c) 
	chr_dev_init();									// 字符设备初始化. 目前该函数为空. (chr_drv/tty_io.c)
 	tty_init();										// tty 初始化. (chr_drv/tty_io.c)
	time_init();									// 设置开机启动时间.
 	sched_init();									// 调度程序初始化(加载任务 0 的 tr, ldtr). (kernel/sched.c)
	// 高速缓冲区用于缓冲读/写块设备(比如硬盘)中的数据.
	buffer_init(buffer_memory_end);					// 高速缓冲区管理初始化, 建立内存缓冲区链表等. 一页大小为 1KB. (fs/buffer.c)
	hd_init();										// 硬盘初始化. (blk_drv/hd.c)
	floppy_init();									// 软驱初始化. (blk_drv/floppy.c)
	sti();											// 所有初始化工作都完了, 于是开启中断(注意, 只能屏蔽硬件中断而不能屏蔽软件中断).
	// 打印内核初始化完毕
	// Log(LOG_INFO_TYPE, "<<<<< Linux0.12 Kernel Init Finished, Ready Start Process0 >>>>>\n");
	// 下面过程通过在堆栈中构建 IRET 指令的返回参数, 利用中断返回指令切换到任务 0 中执行(在用户特权级下执行).
	// NOTE: 并不是通过任务切换来实现的, 只是通过 iret 来自动加载用户态下的各个段描述符来实现特权级的切换.
	// cs 由 0x8 切换成 0xf 即由 0b-0000-1000 切换成 0b-0000-1111 即由 GDT 中的代码段切换为 LDT 中的代码段. 
	// 特权级 CPL 由 0 切换成 3, 在用户态下执行.
	move_to_user_mode();							// 切换到用户态(CPL = 3)下执行. (include/asm/system.h)
	// fork 一个子进程 TASK-1, 并通过时钟中断切换到 TASK-1 中执行 init() 函数, 执行初始化操作.
	if (!fork_for_process0()) {						/* we count on this going ok */
		// TASK-0 中不会进入到这里(返回值不为 0, 是子进程的 pid), 
		// 但是子进程 TASK-1 (返回值 eax 为 0, 在创建进程的时候设置的 eax = 0)会进入到这里来执行.
		init();										// 在新建的子进程(TASK-1 即 init 进程)中执行.
	}
	/*
	 *   NOTE!!   For any other task 'pause()' would mean we have to get a
	 * signal to awaken, but task0 is the sole exception (see 'schedule()')
	 * as task 0 gets activated at every idle moment (when no other tasks
	 * can run). For task0 'pause()' just means we go check if some other
	 * task can run, and if not we return here.
	 */
	/*
	 * 注意!! 对于任何其他任务, 'pause()' 将意味着我们必须等待收到一个信号才会返回就绪态, 但任务 0(task0) 是唯一例外情况(参见 'schedule()'), 
	 * 因为任务 0 在任何空闲时间里都会被激活(当没有其他任务在运行时), 因此对于任务 0 'pause()' 仅意味着我们返回来查看是否有其他任务可以运行, 
	 * 如果没有的话我们就回到这里, 一直循环执行 'pause()'.
	 */
	// pause() 系统调用(kernel/sched.c)会把任务 0 转换成可中断等待状态, 再执行调度函数. 
	// 但是调度函数只要发现系统中没有其他任务可以运行时就会切换到任务 0, 是不信赖于任务 0 的状态.
	for(;;)
		__asm__("int $0x80"::"a" (__NR_pause):);					// 即执行系统调用 pause() ==> include/linux/sys.h#sys_call_table[29].
	// pause() 函数会调用 schedule() 函数来执行调度程序.
}

// 下面函数产生格式化信息并输出到标准输出设备 stdout(1), 这里是指屏幕上显示. 参数 '*fmt' 指定输出将采用的格式, 参见标准 C 语言书籍.
// 该子程序正好是 vsprintf 如何使用的一个简单例子. 该程序使用 vsprintf() 将格式化的字符串放入 printbuf 缓冲区, 
// 然后用 write() 将缓冲区的内容输出到标准设备(1 -- stdout). vsprintf() 函数的实现见 kernel/vsprintf.c.
int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1, printbuf, i = vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

// 在 main() 中已经进行子系统初始化, 包括内存管理, 各种硬件设备和驱动程序. init() 函数在任务 0 第 1 次创建的子进程(任务 1)中.
// 它首先对第一个将要执行的程序(shell)的环境进行初始化, 然后以登录 shell 方式加载程序并执行之.
void init(void)
{
	int pid, i, fd;
	// setup() 是一个系统调用. 用于读取硬盘参数和分区表信息并加载虚拟盘(若存在的话)和安装根文件系统设备. 
	// 该函数用 60 行上 _syscall1()的宏定义, 对应函数是 sys_setup(), 在块设备子目录 (kernel/blk_drv/hd.c).
	setup((void *) &drive_info);
	// 下面以读写访问方式打开设备 "/dev/tty0", 它对应终端控制台. 由于这是第一次打开文件操作, 因此产生的文件句柄号(文件描述符)肯定是 0.
	// 该句柄是 UNIX 类操作系统默认的控制台标准输入句柄 stdin. 
	// 这里再把它以读和写的方式分别打开是为了复制产生标准输出(写)句柄 stdout 和标准出错输出句柄 stderr.
	// 函数前面的 "(void)" 前缀用于表示强制函数无需返回值.
	(void) open("/dev/tty1", O_RDWR, 0); 				// (fs/open.c)
	(void) dup(0);										// 复制句柄, 产生句柄 1 号 -- stdout 标准输出设备. (fs/fcntl.c sys_dup())
	(void) dup(0);										// 复制句柄, 产生句柄 2 号 -- stderr 标准出错输出设备.
	// 下面打印缓冲区块数和总字节数, 每块 1024 字节, 以及主内存区空闲内存字节数.
	printf("<<<<< %d buffers = %d bytes buffer space >>>>>\n\r", NR_BUFFERS, NR_BUFFERS * BLOCK_SIZE); 	// 高速缓冲区.
	printf("<<<<< Free mem: %d bytes >>>>>\n\r", memory_end - main_memory_start); 						// 主内存.
	// 下面 fork() 用于创建一个子进程(TASK-2). 
	// 对于被创建的子进程, fork() 将返回 0 值, 对于原进程(父进程)则返回子进程的进程号 pid. 
	// TASK-2 关闭了文件句柄 0(stdin), 以只读方式打开 /etc/rc 文件, 
	// 并使用 execve() 函数将进程自身替换成 /bin/sh 程序(即 shell 程序), 然后执行 /bin/sh 程序. 
	// 所携带的参数和环境变量分别由 argv_rc 和 envp_rc 数组给出. 
	// 关闭句柄 0 并立刻打开 /etc/rc 文件的作用是把标准输入 stdin 重定向到 /etc/rc/ 文件. 
	// 这样 shell 程序 /bin/sh 就可以运行 rc 文件中设置的命令. 
	// 由于这里 sh 的运行方式是非交互式的, 因此在执行完 rc 文件中的命令后就会立刻退出, 进程 2 也随之结束. 
	// 并于 execve() 函数说明请参见 fs/exec.c 程序.
	// 函数 _exit() 退出时的出错码 1 - 操作未许可; 2 -- 文件或目录不存在.
	if (!(pid = fork())) { 											// (kernel/sys_call.s)
		// 以下代码是在 Task-2 中执行.
		close(0); 													// int 0x80 中断, __NR_close. (lib/close.c) (fs/open.c sys_close())
		if (open("/etc/rc", O_RDONLY, 0))
			_exit(1);												// 若打开文件失败, 则退出(lib/_exit.c).
		execve("/bin/sh", argv_rc, envp_rc);						// 替换成 /bin/sh 程序并执行.
		_exit(2);													// 若 execve() 执行失败则退出.
    }
	// 下面是父进程(Task-1)执行的语句. wait() 等待子进程停止或终止, 返回值应是子进程的进程号(pid). 
	// 这三句的作用是父进程等待子进程的结束. &i 是存放返回状态信息的位置. 如果 wait() 返回值不等于子进程号, 则继续等待.
  	if (pid > 0)
		while (pid != wait(&i));
	// 如果执行到这里, 说明刚创建的子进程的执行已停止或终止了. 
	// 下面循环中首先再创建一个子进程, 如果出错, 则显示 "初始化程序创建子进程失败" 信息并继续执行. 
	// 对于所创建的子进程将关闭所有以前还遗留的句柄(stdin, stdout, stderr), 新创建一个会话并设置进程组号,
	// 然后重新打开 /dev/tty0 作为 stdin, 并复制成 stdout 和 stderr. 再次执行系统解释程序 /bin/sh. 
	// 但这次执行所选用的参数和环境数组另选了一套. 然后父进程再次运行 wait() 等等. 
	// 如果子进程又停止了执行, 则在标准输出上显示出错信息 "子进程 pid 停止了运行, 返回码是 i",
	// 然后继续重试下去..., 形成 "大" 死循环.
	while (1) {
		if ((pid = fork()) < 0) {
			printf("Fork failed in init %c\r\n", ' ');
			continue;
		}
		if (!pid) {                             					// 新的子进程.
			close(0); close(1); close(2);
			setsid();                       						// 创建一新的会话期, 见后面说明.
			(void) open("/dev/tty1", O_RDWR, 0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh", argv, envp));
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r", pid, i);
		sync();
	}
	_exit(0);														/* NOTE! _exit, not exit() */
}