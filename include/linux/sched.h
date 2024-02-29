#ifndef _SCHED_H
#define _SCHED_H

#define HZ 100									// 定义系统时钟滴答频率(100Hz, 每个滴答 10ms)

#define NR_TASKS		64						// 系统中同时最多任务(进程)数.
#define TASK_SIZE		0x04000000				// 每个任务的长度(64MB).
#define LIBRARY_SIZE	0x00400000				// 动态加载库长度(4MB).

#if (TASK_SIZE & 0x3fffff)
#error "TASK_SIZE must be multiple of 4M"		// 任务长度必须是 4MB 的倍数.
#endif

#if (LIBRARY_SIZE & 0x3fffff)
#error "LIBRARY_SIZE must be a multiple of 4M"	// 库长度也必须是 4MB 的倍数.
#endif

#if (LIBRARY_SIZE >= (TASK_SIZE/2))
#error "LIBRARY_SIZE too damn big!"				// 加载库的长度不得大于任务长度的一半.
#endif

#if (((TASK_SIZE>>16)*NR_TASKS) != 0x10000)
#error "TASK_SIZE*NR_TASKS must be 4GB"			// 任务长度 * 任务总个数必须为 4GB.
#endif

// 在进程逻辑地址空间中动态库被加载的位置(60MB).
#define LIBRARY_OFFSET (TASK_SIZE - LIBRARY_SIZE)

// 下面宏 CT_TO_SECS 和 CT_TO_USECS 用于把系统当前嘀嗒数转换成用秒值加微秒值表示.
#define CT_TO_SECS(x)	((x) / HZ)
#define CT_TO_USECS(x)	(((x) % HZ) * 1000000 / HZ)

#define FIRST_TASK task[0]						// 任务 0 比较特殊, 所以特意给它单独定义一个符号.
#define LAST_TASK task[NR_TASKS - 1]			// 指向任务数组中的最后一项任务.

#include <linux/head.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>

#if (NR_OPEN > 32)
#error "Currently the close-on-exec-flags and select masks are in one long, max 32 files/proc"
#endif

// 这里定义了进程运行时可能处的状态.
#define TASK_RUNNING			0	// 进程正在运行或已准备就绪.
#define TASK_INTERRUPTIBLE		1	// 进程处于可中断等待状态.
#define TASK_UNINTERRUPTIBLE	2	// 进程处于不可中断等待状态, 主要用于 I/O 操作等待.
#define TASK_ZOMBIE				3	// 进程处于僵死状态, 已经停止运行, 但父进程还没发信号.
#define TASK_STOPPED			4	// 进程已停止.

#ifndef NULL
#define NULL ((void *) 0)			// 定义 NULL 为空指针.
#endif

// 复制进程的页目录页表. Linus 认为这是内核中最复杂的函数之一.(mm/memory.c)
extern int copy_page_tables(unsigned long from, unsigned long to, long size);
// 释放页表所指定的内存块及页表本身(mm/memory.c)
extern int free_page_tables(unsigned long from, unsigned long size);

// 调度程序的初始化函数(kernel/sched.c)
extern void sched_init(void);
// 进程调度函数(kernel/sched.c)
extern void schedule(void);
// 异常(陷阱)中断处理初始化函数, 设置中断调用门并允许中断请求信号.(kernel/traps.c)
extern void trap_init(void);
// 显示内核出错信息, 然后进入死循环(kernel/panic.c)
extern void panic(const char * str);
// 往 tty 上写指定长度的字符串.(kernel/chr_drv/tty_io.c).
extern int tty_write(unsigned minor,char * buf,int count);

typedef int (*fn_ptr)();			// 定义函数指针类型.

// 下面是数学协处理器使用的结构，主要用于保存进程切换时 i387 的执行状态信息。
struct i387_struct {
	long	cwd;            	// 控制字(Control word).
	long	swd;            	// 状态字(Status word).
	long	twd;            	// 标记字(Tag word).
	long	fip;            	// 协处理器代码指针.
	long	fcs;            	// 协处理器代码段寄存器.
	long	foo;            	// 内存操作数的偏移位置.
	long	fos;            	// 内存操作数的段值.
	long	st_space[20];		/* 8*10 bytes for each FP-reg = 80 bytes */
};                              /* 8 个 10 字节的协处理器累加器。 */

// 任务状态段数据结构.
// 分为静态字段和动态字段, 静态字段的值是在任务被创建时设置的, 通过不会改变它们. 
// 动态字段在当任务切换而被挂起时，处理器会动态更新动态字段的内容.
struct tss_struct {
	// 在任务切换时更新. 该字段允许任务使用 iret 指令切换到前一个任务. 
	long	back_link;			/* 16 high bits zero */ // 前一任务链接(TSS 选择符). 				   [动态字段]
	long	esp0; 										// 特权级 0 (内核态)使用的堆栈指针. 			[静态字段]
	long	ss0;				/* 16 high bits zero */ // 特权级 0 (内核态)使用的堆栈选择符. 			[静态字段]
	long	esp1; 										// 特权级 1 (内核态)使用的堆栈指针. 			[静态字段]
	long	ss1;				/* 16 high bits zero */ // 特权级 1 (内核态)使用的堆栈选择符. 			[静态字段]
	long	esp2; 										// 特权级 2 (内核态)使用的堆栈指针. 		    [静态字段]
	long	ss2;				/* 16 high bits zero */ // 特权级 2 (内核态)使用的堆栈选择符. 			[静态字段]
	long	cr3; 										// 控制寄存器, 含有任务使用的页目录物理基地址. 	  [静态字段]
	long	eip; 										// 指令指针, 在切换之前保存 eip 寄存器的内容. 	 [动态字段]
	long	eflags; 									// 标志寄存器, 在切换之前保存 EFLAGS 中的内容.   [动态字段]
	long	eax; 										// 以下字段用于保存通用寄存器的值, 都是[动态字段].
	long	ecx;
	long	edx;
	long	ebx;
	long	esp;
	long	ebp;
	long	esi;
	long	edi;
	long	es;					/* 16 high bits zero */ // 段选择符字段, 保存段寄存器的中的内容(可见部分, 见p96 图4-12).	[动态字段]
	long	cs;					/* 16 high bits zero */ // 代码段选择符字段, 保存当前任务的代码段选择符.  					[动态字段]
	long	ss;					/* 16 high bits zero */ // 堆栈段选择符字段, 保存当前任务的堆栈段选择符. 					[动态字段]
	long	ds;					/* 16 high bits zero */ // 数据段选择符字段, 保存当前任务的数据段选择符. 					[动态字段]
	long	fs;					/* 16 high bits zero */ // 段选择符字段, 保存段寄存器的中的内容(可见部分, 见p96 图4-12). 	[动态字段]
	long	gs;					/* 16 high bits zero */ // 段选择符字段, 保存段寄存器的中的内容(可见部分, 见p96 图4-12). 	[动态字段]
	long	ldt;				/* 16 high bits zero */ // LDT 段选择符, 含有当前任务的 LDT 段的选择符.					  [静态字段]
	long	trace_bitmap;		/* bits: trace 0, bitmap 16-31 */
	struct i387_struct i387;
};

// 下面是任务(进程)数据结构, 或称为进程描述符.
// long state							任务的运行状态(-1 不可运行, 0 可运行(就绪), >0 已停止).
// long counter							任务运行时间计数(递减)(滴答数), 运行时间片.
// long priority						优先数. 任务开始运行时 counter = priority, 越大运行越长.
// long signal							信号位图, 每个比特位代表一种信号, 信号值 = 位偏移值 + 1.
// struct sigaction sigaction[32]		信号执行属性结构, 对应信号将要执行的操作和标志信息.
// long blocked							进程信号屏蔽码(对应信号位图).
// -----------------------------
// int exit_code						任务执行停止的退出码, 其父进程会取.
// unsigned long start_code				代码段地址.
// unsigned long end_code				代码长度(字节数).
// unsigned long end_data				代码长度 + 数据长度(字节数)
// unsigned long brk					总长度(字节数)
// unsigned long start_stack			堆栈段地址.
// long pid								进程标识号(进程号)
// long pgrp							进程组号.
// long session							会话号.
// long leader							会话首领.
// int	groups[NGROUPS];				进程所属组号. 一个进程可属于多个组.
// struct task_struct *p_pptr			指向父进程的指针
// struct task_struct *p_cptr			指向最新子进程的指针.
// struct task_struct *p_ysptr			指向比自己后创建的相邻进程的指针.
// struct task_struct *p_osptr 			指向比自己早创建的相邻进程的指针.
// unsigned short uid					用户标识号(用户 id).
// unsigned short euid					有效用户 id.
// unsigned short suid					保存的用户 id.
// unsigned short gid					组标识号(级 id)
// unsigned short egid					有效级 id.
// unsigned short sgid					保存的组 id.
// unsigned long timeout				内核定时超时值
// unsigned long alarm					报警定时值(滴答数)
// long utime							用户态运行时间(滴答数)
// long stime							系统态运行时间(滴答数)
// long cutime							子进程用户态运行时间.
// long cstime							子进程系统态运行时间.
// long start_time						进程开始运行时刻.
// struct rlimit rlim[RLIM_NLIMITS]		进程资源使用统计数组.
// unsigned int flags					各进程的标志.
// unsigned short used_math				标志: 是否使用了协处理器.
// ------------------------------------------------------------------------------
// int tty;								进程使用 tty 终端的子设备号. -1 表示没有使用.
// unsigned short umask					文件创建属性屏蔽位.
// struct m_inode * pwd					当前工作目录 i 节点结构指针.
// struct m_inode * root				根目录 i 节点结构指针.
// struct m_inode * executable			执行文件 i 节点结构指针.
// struct m_inode * library				被加载库文件 i 节点结构指针.
// unsigned long close_on_exec			执行时关闭文件句柄位图标志.(include/fcntl.h)
// struct file * filp[NR_OPEN]			文件结构指针表, 最多 32 项. 表项号即是文件描述符的值.
// struct desc_struct ldt[3]			局部描述符表, 0 - 空, 1 - 代码段 cs, 2 - 数据和堆栈段 ds 和 ss.
// struct tss_struct tss				进程的任务状态段信息结构.
// ==============================================================================
struct task_struct {
	/* these are hardcoded - don't touch */
	// 下面这几个字段是硬编码字段
	long state;							// 任务的运行状态(-1 不可运行, 0 可运行(就绪), >0 已停止)
	long counter;						// 任务运行时间计数(递减)(滴答数), 运行时间片, 越大运行时间越长
	long priority;						// 优先级. 任务开始运行时 counter = priority, 越大运行越长
	long signal;						// 信号位图, 每个比特位代表一种信号, 信号值 = 位偏移值 + 1
	struct sigaction sigaction[32];		// 信号执行属性结构, 对应信号将要执行的操作和标志信息
	long blocked;						// 进程信号屏蔽码(对应信号位图)

	/* various fields */
	int exit_code;						// 任务执行停止的退出码, 其父进程会取.
	unsigned long start_code;			// 代码段地址
	unsigned long end_code;				// 代码长度(字节数)
	unsigned long end_data;				// 代码长度 + 数据长度(字节数)
	unsigned long brk;					// 总长度(字节数)
	unsigned long start_stack;			// 堆栈段地址
	long pid;							// 进程标识号(进程号)
	long pgrp;							// 进程组号
	long session;						// 会话号
	long leader;						// 会话首领
	int	groups[NGROUPS];				// 进程所属组号. 一个进程可属于多个组
	/*
	 * pointers to parent process, youngest child, younger sibling,
	 * older sibling, respectively.  (p->father can be replaced with
	 * p->p_pptr->pid)
	 */
	struct task_struct *p_pptr;			// 指向父进程的指针
	struct task_struct *p_cptr;			// 指向最新子进程的指针
	struct task_struct *p_ysptr;		// 指向比自己后创建的相邻进程的指针
	struct task_struct *p_osptr;		// 指向比自己早创建的相邻进程的指针
	unsigned short uid;					// 用户标识号(用户 id)
	unsigned short euid;				// 有效用户 id
	unsigned short suid;				// 保存的用户 id
	unsigned short gid;					// 组标识号(级 id)
	unsigned short egid;				// 有效级 id
	unsigned short sgid;				// 保存的组 id
	unsigned long timeout;				// 内核定时超时值(单位: 滴答数) ==> 应该是指明该任务在系统运行多长时间后超时
	unsigned long alarm;				// 报警定时值(单位: 滴答数) ==> 指明该任务在系统运行多长时间后时间到
	long utime;							// 当前进程用户态总运行时间(滴答数)
	long stime;							// 当前进程系统态总运行时间(滴答数)
	long cutime;						// 子进程用户态运行时间
	long cstime;						// 子进程系统态运行时间
	long start_time;					// 进程开始运行时刻.
	struct rlimit rlim[RLIM_NLIMITS];	// 进程资源使用统计数组.
	/* per process flags, defined below */
	unsigned int flags;					// 各进程的标志.
	unsigned short used_math;			// 标志: 是否使用了协处理器.

	/* file system info */
	/* -1 if no tty, so it must be signed */
	int tty;							// 进程使用 tty 终端的子设备号. -1 表示没有使用.
	unsigned short umask;				// 文件创建属性屏蔽位.
	struct m_inode * pwd;				// 当前工作目录 i 节点结构指针.
	struct m_inode * root;				// 根目录 i 节点结构指针.
	struct m_inode * executable;		// 执行文件 i 节点结构指针.
	struct m_inode * library;			// 被加载库文件 i 节点结构指针.
	unsigned long close_on_exec;		// 执行时关闭文件句柄位图标志. (include/fcntl.h) 见下面注释.
	struct file * filp[NR_OPEN];		// 进程打开的文件结构指针表, 最多 32 项. 表项号(索引值)即是文件描述符的值.
	/* ldt for this task 0 - zero 1 - cs 2 - ds&ss */
	struct desc_struct ldt[3];			// 局部描述符表, 0 - 空, 1 - 代码段 cs, 2 - 数据和堆栈段 ds 和 ss.
	/* tss for this task */
	struct tss_struct tss;				// 进程的任务状态段信息结构.
};
/* close_on_exec 是一个进程所有文件句柄的位图标志. 每个位代表一个打开着的文件描述符, 用于确定在调用系统调用 execve() 时需要关闭的文件句柄. 
   当程序使用 fork() 函数创建一个子进程时, 通常会在该子进程中调用 execve() 函数加载执行另一个新程序. 此时子进程中开始执行新程序. 
   若一个文件句柄 close_on_exec 中的对应位被置位, 那么在执行 execve() 时该对应文件句柄将被关闭, 否则该文件句柄将始终处于打开状态. */

/*
 * Per process flags
 */
/* 每个进程的标志 */    /* 打印对齐警告信息. 还未实现, 仅用于 486 */
#define PF_ALIGNWARN	0x00000001	/* Print alignment warning msgs */
					/* Not implemented yet, only for 486*/

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x9ffff(= 640kB)
 */
/*
 * INIT_TASK 用于设置第 1 个任务表, 若想修改, 责任自负!
 * 基址 Base = 0, 段长 limit = 0x9ffff(= 640KB).
 */
// 对应上面任务结构的第 1 个任务(TASK-0)的信息.
#define INIT_TASK {\
	/* state etc */ 0, 15, 15, \
	/* signals */	0, {{},}, 0, \
	/* ec,brk... */	0, 0, 0, 0, 0, 0, \
	/* pid etc.. */	0, 0, 0, 0, \
	/* suppl grps*/ {NOGROUP,}, \
	/* proc links*/ &init_task.task, 0, 0, 0, \
	/* uid etc */	0, 0, 0, 0, 0, 0, \
	/* timeout */	0, 0, 0, 0, 0, 0, 0, \
	/* rlimits */   { {0x7fffffff, 0x7fffffff}, {0x7fffffff, 0x7fffffff},  \
		  			{0x7fffffff, 0x7fffffff}, {0x7fffffff, 0x7fffffff}, \
		  			{0x7fffffff, 0x7fffffff}, {0x7fffffff, 0x7fffffff}}, \
	/* flags */		0, \
	/* math */		0, \
	/* fs info */	-1, 0022, NULL, NULL, NULL, NULL, 0, \
	/* filp */		{NULL,}, \
	/* ldt */ \
					{ \
						{0, 0}, \
						{0x9f, 0xc0fa00}, /* 段基地址 0x0; 段限长 636KB; 特权级 DPL = 3; 代码段, 可读/可执行 */\
						{0x9f, 0xc0f200}, /* 段基地址 0x0; 段限长 636KB; 特权级 DPL = 3; 数据段, 可读/可写 */\
					}, \
	/*tss*/ \ 		// 前一任务链接的 tss 段选择符,             esp0, 
					{        0,                   PAGE_SIZE + (long) &init_task, \
					//  ss0,  esp1, ss1,  esp2,  ss2,    cr3(页目录基地址寄存器, pdbr)
						0x10,  0,    0,     0,     0,       (long) &pg_dir, \
					// eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi
		 				0,     0,    0,   0,   0,   0,   0,   0,   0,   0, \
					//   ex,   cs,   ss,   ds,   fs,   gs
		 				0x17, 0x17, 0x17, 0x17, 0x17, 0x17, \
					// LDT 段选择符,    IO 位图地址
		 				_LDT(0),      0x80000000, \
						{} \
					}, \
}
// 上面 tss 中的第二个 0x17 表示 cs 的段选择符, 0x17 = 0b-0001-0111 表示 LDT 中的第 1 个(从 0 开始)段描述符. RPL = 3.
// 至于 DPL 要去对应的 LDT 中看, 
// 即上面的 ldt 字段中的 (0x9f,0xc0fa00) = 0x00c0[f]a00-0000009f 对应的 DPL = 0xf = 0b1-11-1 对应的 DPL = 11 = 3, 即 DPL 为 3.
// tss 结构中的第二/三项分别为 esp0 = PAGE_SIZE + (long) &init_task, 即指向该内存页(4K)的末尾处, ss0 = 0x10, 即内核数据段.
// 0x00c0fa00-0000009f:	段基地址 = 0x00000000; 段限长 = 0x0009f(0x9f*4=636KB); DPL = 0b11(特权级为 3); 
// 						P = 1(段存在); S = 1(描述符类型: 代码或数据); TYPE = 0b1010(代码段: 可读/可执行); 
// 						G = 0b1(颗粒度: 4KB 为单位); D/B = 0b1; AVL = 0b1; 

extern struct task_struct *task[NR_TASKS];								// 任务指针数组.
extern struct task_struct *last_task_used_math;							// 上一个使用过协处理器的进程
extern struct task_struct *current;										// 当前运行进程结构指针变量.
//extern struct task_struct *test_task;
extern unsigned long volatile jiffies;									// 从开机开始算起的滴答数(10ms/滴答).
extern unsigned long startup_time;										// 开机时间. 从 1970:0:0:0:0 开始计时的秒数.
extern int jiffies_offset;												// 用于累计需要调整的时间滴答数.

#define CURRENT_TIME (startup_time + (jiffies + jiffies_offset) / HZ)	// 当前时间(秒数).

// 添加定时器函数(定时时间 jiffies 嘀嗒数, 定时到时调用函数 *fn()). (kernel/sched.c)
extern void add_timer(long jiffies, void (*fn)(void));
// 不可中断的等待睡眠.(kernel/sched.c)
extern void sleep_on(struct task_struct ** p);
// 可中断的等待睡眠.(kernel/sched.c)
extern void interruptible_sleep_on(struct task_struct ** p);
// 明确唤醒睡眠的进程.(kernel/sched.c)
extern void wake_up(struct task_struct ** p);
// 检查当前进程是否在指定的用户组 grp 中.
extern int in_group_p(gid_t grp);

/*
 * Entry into gdt where to find first TSS. 0-nul, 1-cs, 2-ds, 3-syscall
 * 4-TSS0, 5-LDT0, 6-TSS1 etc ...
 */
/*
 * 寻找第 1 个 TSS 在全局表中的入口. 0 - 没有用 null, 1 - 内核代码段 cs, 2 - 内核数据段 ds, 
 * 3 - 系统段 syscall, 4 - 任务状态段 TSS0, 5 - 局部表 LTD0, 6 - 任务状态段 TSS1 等.
 */
// 从英文注释可以猜想到 Linus 当时曾想把系统调用的代码专门放在 GDT 表中第 4 个独立的段中. 
// 但后来并没有那样做, 于是就一直把 GDT 表中第 4 个描述符项(上面 syscall 项)闲置在一旁.
// 下面定义宏: 全局表第 1 个任务状态段(TSS)描述符的选择符索引号.
#define FIRST_TSS_ENTRY 4 							// 第一个任务状态段的索引值
// 全局表中第 1 个局部描述符表(LDT)描述符的选择符索引号.
#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY + 1)
// 宏定义, 计算在全局表中第 n 个任务的 TSS 段描述符的选择符值(偏移量, 偏移字节数).
// 因每个描述符占 8 字节, 因此 FIRST_TSS_ENTRY << 3 (*8) 表示描述符在 GDT 表中的起始偏移位置.
// 因为每个任务使用 1 个 TSS 和 1 个 LDT 描述符, 共占用 16 字节, 因此需要 n << 4 (n*16) 来表示对应 TSS 起始位置. 
// 该宏得到的值正好也是该 TSS 的选择符值. 
// [比如 TASK1: (1<<4 + 4<<3) = 48 = 0b-00110-000(低三位为其它标志) 对应用索引值为 110 = 6(即第 6 个段描述符)].
#define _TSS(n) ((((unsigned long) n) << 4) + (FIRST_TSS_ENTRY << 3))
// 宏定义, 计算在全局表中第 n 个任务的 LDT 段描述符的选择符值(偏移量)
#define _LDT(n) ((((unsigned long) n) << 4) + (FIRST_LDT_ENTRY << 3))
// 宏定义, 把第 n 个任务的 TSS 段选择符加载到任务寄存器 TR 中.
#define ltr(n) __asm__("ltr %%ax"::"a" (_TSS(n)))
// 宏定义, 把第 n 个任务的 LDT 段选择符加载到局部描述符表寄存器 LDTR 中.
#define lldt(n) __asm__("lldt %%ax"::"a" (_LDT(n)))
// 取当前运行任务的任务号(是任务数组中的索引值, 与进程号 pid 不同).
// 返回: n - 当前任务号. 用于(kernel/traps.c).
#define str(n) \
__asm__(\
	"str %%ax\n\t"  				/* 将任务寄存器中 TSS 段的选择符复制到 ax 中 */\
	"subl %2, %%eax\n\t"  			/* (eax - FIRST_TSS_ENTRY*8) -> eax */\
	"shrl $4, %%eax"  				/* (eax/16) -> eax = 当前任务号 */\
	:"=a" (n) \
	:"a" (0), "i" (FIRST_TSS_ENTRY << 3))

/*
 * switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 * This also clears the TS-flag if the task we switched to has used
 * tha math co-processor latest.
 */
/*
 * switch_to(n) 将切换当前任务到任务 nr, 即 n. 首先检测任务 n 不是当前任务, 如果是则什么也不做退出. 
 * 如果我们切换的任务最近(上次运行)使用过数学协处理器的话, 则还需复位控制器 cr0 中的 TS 标志.
 */
// 跳转到一个任务的 TSS 段选择符组成的地址处会造成 CPU 进行任务切换操作.
// 输入: %0 - 指向 __tmp;		     %1 - 指向 __tmp.b 处, 用于存放新 TSS 选择符.
//      dx - 新任务 n 的 TSS 选择符;  ecx - 新任务 n 的任务结构指针 task[n].
// 其中临时数据结构 __tmp 用于组建远跳转(far jump)指令的操作数. 
// 该操作数由 4 字节偏移地址和 2 字节的段选择符组成. 
// 因此 __tmp 中 a 的值是 32 位偏移值, 而的低 2 字节是新 TSS 段的选择符(高 2 字节不用). 
// 中转与 TSS 段选择符会造成任务切换到该 TSS 对应的进程.
// 对于造成任务切换的长跳转, a 值无用. 内存间接跳转指令(`ljmp *%0`)使用 6 字节操作数作为跳转目的地的长指针, 
// 其格式为: jmp 16 位段选择符 : 32 位偏移值. 但在内存中操作数的表示顺序与这里正好相反. 
// 任务切换回来之后, 在判断原任务上次执行是否使用过协处理器时,
// 是通过将原任务指针与保存在 last_task_used_math 变更中的上次使用过协处理器指针进行比较而作出的, 
// 参见文件 kernel/sched.c 中有关 math_state_restore() 函数的说明. 
// 唯一的调用方是 schedule() 方法, 也即系统启动后只有 schedule 会进行任务切换.
#define switch_to(n) { \
struct {long a, b;} __tmp; 					/* long 占 4bytes */\
__asm__(\
	"cmpl %%ecx, current\n\t"  				/* 任务 n 是当前任务吗?(current == task[n]?) */\
	"je 1f\n\t"  							/* 是, 则什么都不做退出 */\
	"movw %%dx, %1\n\t"  					/* 将新任务 TSS 的 16 位选择符存入 __tmp.b 中 */\
	"xchgl %%ecx, current\n\t"  			/* 寄存器交换操作, 将 current 指向要切换到的任务, 并将 ecx 指向要切出的任务 ==> current = task[n]; ecx = 被切换出的任务地址. */\
	"ljmp *%0\n\t"  						/* 执行长跳转至 *&__tmp (数值是 task[n] 的 TSS 段选择符), 造成任务切换. 在任务切换回来后才会继续执行下面的语句 */\
	"cmpl %%ecx, last_task_used_math\n\t"  	/* 原任务上次使用过协处理器吗? */\
	"jne 1f\n\t"  							/* 没有则跳转, 退出 */\
	"clts\n"  								/* 原任务上次使用过协处理器, 则清 cr0 中的任务切换标志 TS */\
	"1:"  									/* 函数返回 */\
	::"m" (*&__tmp.a), "m" (*&__tmp.b), \
	"d" (_TSS(n)), "c" ((long) task[n]));  	/* 将任务 n 的 TSS 段选择符值放入 edx 中, 将 task[n] 的地址放入 ecx 中 */ \
}

// 页面地址对准(在内核代码中同有任何地方引用!!)
#define PAGE_ALIGN(n) (((n) + 0xfff) & 0xfffff000)

/*
#define _set_base(addr,base) \
__asm__ __volatile__ ("movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %%dl,%1\n\t" \
	"movb %%dh,%2" \
	::"m" (*((addr)+2)), \
	  "m" (*((addr)+4)), \
	  "m" (*((addr)+7)), \
	  "d" (base) \
	:)
*/

// 设置位于地址 addr 处描述符中的各基地址字段(基地址是 base).
// %0 - 地址 addr 偏移 2; %1 - 地址 addr 偏移 4; %2 - 地址 addr 偏移 7; edx - 基地址 base.
#define _set_base(addr, base) do { unsigned long __pr; \
__asm__ __volatile__ (\
		"movw %%dx, %1\n\t"  			/* 基地址 base 低 16 位(位 15-0) -> [addr+2] */\
		"rorl $16, %%edx\n\t"  			/* edx 中基址高 16 位(位 31-16) -> dx */\
		"movb %%dl, %2\n\t"  			/* 基址高 16 位中的低 8 位(位 23-16) -> [addr+4] */\
		"movb %%dh, %3\n\t"  			/* 基址高 16 位中的高 8 位(位 31-24) -> [addr+7] */\
		:"=&d"(__pr) \
		:"m"(*((addr) + 2)), \
		"m"(*((addr) + 4)), \
		"m"(*((addr) + 7)), \
		"0"(base) \
		); } while(0)

// 设置位于地址 addr 处描述符中的段限长字段(段长是 limit).
// %0 - 地址 addr; %1 - 地址 addr 偏移 6 处; edx - 段长值 limit.
#define _set_limit(addr, limit) \
__asm__(\
	"movw %%dx, %0\n\t"   				/* 段长 limit 低 16 位(位 15-0) -> [addr] */\
	"rorl $16, %%edx\n\t"  				/* edx 中的段长高 4 位(位 19-16) -> dl */\
	"movb %1, %%dh\n\t"  				/* 取原 [addr+6] 字节 -> dh, 其中高 4 位是些标志 */\
	"andb $0xf0, %%dh\n\t" 				/* 清 dh 的低 4 位(将存放段长的位 19-16) */\
	"orb %%dh, %%dl\n\t"  				/* 将原高 4 位标志和段长的高 4 位(位 19-16)合成 1 字节, 并放回 [addr+6] 处 */\
	"movb %%dl, %1" \
	::"m" (*(addr)), \
	  "m" (*((addr) + 6)), \
	  "d" (limit) \
	:)

// 设置局部描述符表中 ldt 描述符的基地址字段.
#define set_base(ldt, base) _set_base( ((char *)&(ldt)) , base )
// 设置局部描述符表中 ldt 描述符的段长字段.
#define set_limit(ldt, limit) _set_limit( ((char *)&(ldt)) , (limit - 1) >> 12 )

// 从地址 addr 处描述符中取段基地址. 功能与 _set_base() 正好相反.
// edx - 存放基地址(__base); %1 - 地址 addr 偏移 2; %2 - 地址 addr 偏移 4; %3 - addr 偏移 7.
#define _get_base(addr) ({\
unsigned long __base; \
__asm__(\
	"movb %3, %%dh\n\t"  				/* 取 [addr+7] 处基地址高 16 位的高 8 位(位 31-24) -> dh */\
	"movb %2, %%dl\n\t"  				/* 取 [addr+4] 处基址高 16 位的低 8 位(位 23-16) -> dl */\
	"shll $16, %%edx\n\t"  				/* 基地址高 16 位移到 edx 中高 16 位处. */\
	"movw %1, %%dx"  					/* 取 [addr+2] 处基址低 16 位(位 16-0) -> dx */\
	:"=&d" (__base)  					/* 从而 edx 中含有 32 位的段基地址 */\
	:"m" (*((addr) + 2)), \
	 "m" (*((addr) + 4)), \
	 "m" (*((addr) + 7))); \
__base;})

/*
static inline unsigned long _get_base(char * addr){
	unsigned long __base;
	__asm__("movb %3,%%dh\n\t" \
			"movb %2,%%dl\n\t" \
			"shll $16,%%edx\n\t" \
			"movw %1,%%dx\n\t" \
			:"=&d"(__base) \
			:"m" (*((addr)+2)), \
			 "m" (*((addr)+4)), \
			 "m" (*((addr)+7)));
	return __base;
}
*/

// 取局部描述符表中 ldt 所指段描述符中的基地址.
#define get_base(ldt) _get_base( ((char *)&(ldt)) )

// 取段选择符 segment 指定的 **描述符** 中的段限长值.
// 指令 lsll 是 Load Segment Limit 的缩写它从指定段描述符中取出分散的限长比特位拼成完整的段限长值放入指定寄存器中. 
// 所得的段限长是实际字节数减 1, 因此这里还需要加 1 后返回.
// %0 - 存放段长值(字节数); %1 - 段选择符 segment.
#define get_limit(segment) ({ \
unsigned long __limit; \
__asm__("lsll %1,%0\n\tincl %0":"=r" (__limit):"r" (segment)); \
__limit;})

#endif
