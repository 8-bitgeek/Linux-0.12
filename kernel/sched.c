/*
 *  linux/kernel/sched.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid(), which just extracts a field from current-task.
 */
/*
 * 'sched.c' 是主要的内核文件. 
 * 其中包括有关高度的基本函数(sleep_on, wakeup, schedule 等)以及一些简单的系统调用函数(比如 getpid(), 仅从当前任务中获取一个字段).
 */
// 下面是调度程序头文件. 定义了任务结构 task_struct, 第 1 个初始任务的数据. 
// 还有一些以宏的形式定义的有关描述符参数设置和获取的嵌入式汇编函数程序.
#include <linux/sched.h>
#include <linux/kernel.h>					// 内核头文件. 含有一些内核常用函数的原形定义.
#include <linux/sys.h>						// 系统调用头文件. 含有 82 个系统调用 C 函数程序, 以 'sys_' 开头.
#include <linux/fdreg.h>					// 软驱头文件. 含有软盘控制器参数的一些定义.
#include <asm/system.h>						// 系统头文件. 定义了设置或修改描述符/中断门等的嵌入式汇编宏.
#include <asm/io.h>							// io 头文件. 定义硬件端口输入/输出宏汇编语句.
// #include <asm/segment.h>

#include <signal.h>

// 该宏取信号 nr 在信号位图中对应位的二进制数值. 信号编号 1-32. 
// 比如信号 5 的位图数值等于 1<<(5-1) = 16 = 00010000.
#define _S(nr) (1 << ((nr) - 1))
// 除了 SIGKILL 和 SIGSTOP 信号以外其他信号都是可阻塞的.
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

// 内核调试函数. 显示任务号 nr 的进程号, 进程状态和内核堆栈空闲字节数(大约).
void show_task(int nr, struct task_struct * p) {
	int i, j = 4096 - sizeof(struct task_struct);

	printk("%d: pid=%d, state=%d, father=%d, child=%d, ", nr, p->pid, p->state, p->p_pptr->pid, p->p_cptr ? p->p_cptr->pid : -1);
	i = 0;
	while (i < j && !((char *)(p + 1))[i]) {			// 检测指定任务数据结构以后等于 0 的字节数.
		i++;
	}
	printk("%d/%d chars free in kstack\n\r", i, j);
	printk("   PC=%08X.", *(1019 + (unsigned long *)p));
	if (p->p_ysptr || p->p_osptr) {
		printk("   Younger sib=%d, older sib=%d\n\r", p->p_ysptr ? p->p_ysptr->pid : -1, p->p_osptr ? p->p_osptr->pid : -1);
	} else {
		printk("\n\r");
	}
}

// 显示所有任务的任务号, 进程号, 进程状态和内核堆栈空闲字节数(大约).
// NR_TASKS 是系统能容纳的最大进程(任务)数量(64 个), 定义在 include/kernel/sched.h
void show_state(void) {
	int i;

	printk("\rTask-info:\n\r");
	for (i = 0; i < NR_TASKS; i++) {
		if (task[i]) {
			show_task(i, task[i]);
		}
	}
}

// PC8253 定时芯片的输入时钟频率约为 1.193180MHz. 
// Linux 内核希望定时器发出中断的频率是 100Hz, 也即每 10ms 发出一次时钟中断. 
// 因此这里 LATCH 是设置 8253 芯片的初值.
#define LATCH (1193180 / HZ)

extern void mem_use(void);              	// 没有任何地方定义和引用该函数。

extern int timer_interrupt(void);			// 时钟中断处理程序(kernel/sys_call.s)
extern int system_call(void);				// 系统调用中断处理程序(kernel/sys_call.s)

// 每个任务(进程)在内核态运行时都有自己的内核态堆栈. 这里定义了任务的内核态堆栈结构.
// 这里定义任务联合(任务结构成员和 stack 字符数组成员). 
// 因为一个任务的数据结构与其内核态堆栈放在同一内存页中, 
// 所以从堆栈段寄存器 ss 可以获得其数据段选择符.
union task_union {
	struct task_struct task;
	char stack[PAGE_SIZE]; 					// 任务内核态堆栈, 占用一页内存 4KB.
};

// 设置初始任务的数据. 初始数据在 ../include/linux/sched.h 中.
static union task_union init_task = {INIT_TASK, };

// 从开机开始算起的滴答数时间值全局变量(10ms/滴答). 系统时钟中断每发生一次即一个滴答. 
// 前面的限定符 volatile, 英文解释是易改变的, 不稳定的意思.
// 这个限定词的含义是向编译器指明变量的内容可能会由于被其他程序修改而变化. 
// 通常在程序中声明一个变量时, 编译器会尽量把它存放在通用寄存器中以提高访问效率, 例如 ebx. 
// 当 CPU 把其值放到 ebx 中后一般就不用再关心该变量在内存中的内容(只从寄存器中取值). 
// 若此时其他程序(例如内核程序或一个中断过程)修改了内存中该变量的值, ebx 中的值并不会随之更新. 
// 为了解决这种情况就创建了 volatile 限定符, 让**代码在引用该变量时一定要从内存中取值**. 
// 这里即是要求 gcc 不要对 jiffies 进行优化处理, 也不要挪动位置, 并且需要从内存中取值. 
// 因为时钟中断处理过程等程序会修改它的值.
unsigned long volatile jiffies = 0; 				// 系统开机时间的滴答数.
unsigned long startup_time = 0;						// 开机时间. 从 1970:0:0:0:0 开始计时的秒数.(kernel/sched.c)
// 这个变量用于累计需要调整的时间滴答数.
int jiffies_offset = 0;								/* # clock ticks to add to get "true
													   time".  Should always be less than
													   1 second's worth.  For time fanatics
													   who like to syncronize their machines
													   to WWV :-) */
/*
 * 为调整时钟而需要增加的时钟滴答, 以获得"精确时间". 这些调整用滴答数的总和不应该超过 1 秒. 
 * 这样做是为了那些对时间精确度要求苛刻的人, 他们培养喜欢自己的机器时间与 WWV 同步 :-
 */
struct task_struct * current = &(init_task.task);	// 当前任务指针(初始化时指向任务 0).
struct task_struct * last_task_used_math = NULL;	// 使用过协处理器任务的指针.

// 定义任务指针数组. 第 0 项为指向任务 0 的任务数据结构.
struct task_struct * task[NR_TASKS] = {&(init_task.task), };

// 定义用户堆栈, 共 1K 项, 容量为 4K 字节. 
// 在内核初始化操作过程中被用作内核栈, 初始化完成以后将被用作 TASK-0 的用户堆栈. 
// 在运行任务 0 之前它是内核栈, 以后用作 TASK-0 和 TASK-1 的用户态栈.
// 下面结构用于设置堆栈 ss:esp(数据段选择符, 指针).
// ss 被设置为内核数据段选择符(0x10), 指针 esp 指在 user_stack 数组最后一项后面. 
// 这是因为 Intel CPU 执行堆栈操作时是先递减堆栈指针 sp 值, 然后在 sp 指针处保存入栈内容.
long user_stack[PAGE_SIZE >> 2]; 						// long 类型占 4 字节, 整个 user_stack 占 4096 字节.

struct {
	long * a; // esp 指针. (long * 和 char * 占用的内存大小是一样和, 区别在于指针自增时, long 类型 +4, 而 char 类型 +1)
	short b; 											// ss 选择符.
} stack_start = {&user_stack[PAGE_SIZE >> 2], 0x10}; 	// 取 user_stack 的数组末尾处作为栈底. 

/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 */
/*
 * 将当前协处理器内容保存到老协处理器状态数组中, 并将当前任务的协处理器内容加载进协处理器.
 */
// 当任务被调度交换过以后, 该函数用以保存原任务的协处理器状态(上下文)并恢复新调度进来的当前任务的协处理器执行状态.
void math_state_restore() {
	// 如果任务没变则返回(上一个任务就是当前任务). 这里 "上一个任务" 是指刚被交换出去的任务.
	if (last_task_used_math == current) {
		return;
	}
	// 在发送协处理器命令之前要先必 WAIT 指令. 如果上个任务使用了协处理器, 则保存其状态.
	__asm__("fwait");
	if (last_task_used_math) {
		__asm__("fnsave %0" : : "m" (last_task_used_math->tss.i387));
	}
	// 现在, las_task_used_math 指向当前任务, 以备当前任务被交换出去时使用. 
	// 此时如果当前任务用过协处理器, 则恢复其状态. 否则的话说明是第一次使用,
	// 于是就向协处理器发初始化命令, 并设置使用协处理器标志.
	last_task_used_math = current;
	if (current->used_math) {
		__asm__("frstor %0" : : "m" (current->tss.i387));
	} else {
		__asm__("fninit" : : );					// 向协处理器发初始化命令.
		current->used_math = 1;					// 设置已使用协处理器标志.
	}
}

/*
 * 'schedule()' is the scheduler function. This is GOOD CODE! There
 * probably won't be any reason to change this, as it should work well
 * in all circumstances (ie gives IO-bound processes good response etc).
 * The one thing you might take a look at is the signal-handler code here.
 *
 * NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 */
/*
 * 'schedule()' 是调度函数. 这是个很好的代码! 没有任何理由对它进行修改, 
 * 因为它可以在所有的环境下工作(比如能够对 IO 边界下得很好的响应等). 
 * 只有一件事值得留意, 那就是这里的信号处理代码.
 *
 * 注意!! 任务 0 是个闲置('idle')任务, 只有当没有其他任务可以运行时才调用它.
 * 它不能被杀死, 也不睡眠. 任务 0 中的状态信息 'state' 是从来不用的.
 */
void schedule(void) {
	int i, next, c;
	struct task_struct ** p;						// 任务结构指针的指针.

reschedule:
	/* check alarm, wake up any interruptible tasks that have got a signal */
	/* 检测 alarm(进程的报警定时值), 唤醒任何已得到信号的可中断任务 */
	for (p = &LAST_TASK; p > &FIRST_TASK; --p) {
		if (*p) { 									// 判断任务是否存在.
			// 如果任务设置了超时时间 timeout(比如读超时, sys_select()), 并且已经超时(jiffies > timeout), 则清除超时时间; 
			// 如果任务是可中断睡眠状态 TASK_INTERRUPTIBLE, 将其置为就绪状态(TASK_RUNNING), 使其恢复运行.
			// jiffies 是系统从开机开始算起的滴答数(10ms/滴答). 
			if ((*p)->timeout && jiffies > (*p)->timeout) { 	// jiffies > timeout 表示已经超时. TODO: sys_select() 函数会设置超时时间.
				(*p)->timeout = 0; 								// 清除超时时间.
				if ((*p)->state == TASK_INTERRUPTIBLE) { 		// 如果进程是可中断睡眠状态, 则唤醒它.
					(*p)->state = TASK_RUNNING; 				// 置为可运行状态, 让其可以被调度执行.
				}
			}
			// 如果设置过任务的定时器 alarm, 并且已经过时间了, 则向任务发送 SIGALRM 信号. 
			// 然后清除定时值, 该信号的默认操作是终止进程(do_signal() 函数对 SIGALRM 的默认处理是调用 exit()). 
			if ((*p)->alarm && jiffies > (*p)->alarm) {
				(*p)->signal |= (1 << (SIGALRM - 1));
				(*p)->alarm = 0; 								// 清除定时值.
			}
			// 唤醒所有接收到不可屏蔽信号的可中断睡眠的进程(因为这里会遍历任务列表), 让其可以继续运行来处理信号.
			// **所以可以说进程是被信号唤醒的!!!**, 而不可中断睡眠的进程不会被唤醒, 即不响应信号.
			// '(_BLOCKABLE & (*p)->blocked)' 得到可被屏蔽的信号, 取反得到不可被屏蔽的信号, SIGKILL 和 SIGSTOP 不能被屏蔽.
			if (((*p)->signal & ~(_BLOCKABLE & (*p)->blocked)) && (*p)->state == TASK_INTERRUPTIBLE) {
				(*p)->state = TASK_RUNNING;
			}
		}
	}

	/* this is the scheduler proper: */ /* 这里是调度程序的主要部分: */
	while (1) {
		c = -1;
		next = 0; 													// 将要切换到的进程项号.
		i = NR_TASKS;
		p = &task[NR_TASKS];
		// 遍历任务列表, 获取时间片最长的就绪状态的任务.
		while (--i) {
			if (!*--p) continue; 									// 跳过空项.

			// 任务处于就绪状态, 并且任务的时间片大于此前的最大时间片(c), 则 next 指向该任务(目前最应该被执行的任务).
			if ((*p)->state == TASK_RUNNING && (*p)->counter > c) { // 找到 counter 更大的 RUNNING(正在运行或者已准备就绪)进程.
				c = (*p)->counter, next = i;
			}
		}
		// 如果 c == -1, 表示当前没有处于 TASK_RUNNING 状态的进程. 此时 next = 0;
		// 因为如果有 RUNNING 状态的进程, 则此时 c >= 0, 因为 (*p)->counter > c 一定成立.
		// 如果此时没有处于 TASK_RUNNING 状态的进程, 并且该函数是由 TASK-0(idle) 进程调用的, 则让系统进入空闲状态, 降低 CPU 消耗.
		if (c == -1 && current == task[0]) { 
			__asm__("hlt");
			goto reschedule; 										// 被时钟中断唤醒时会重新从开始处执行, 完美!
		}

		// 如果找到一个时间片大于 0 的就绪任务, 则退出循环, 并切换到这个任务上运行.
		if (c) break; 													// 如果找到待运行的任务, 则跳出循环后切换到该任务上运行.

		// 如果没有找到时间片大于 0 的就绪任务, 则根据每个任务的优先值, 更新**所有任务**的 counter 值, 然后重新循环比较. 
		// counter 值的计算方式为 counter = counter / 2 + priority. (即原来时间片不为 0 的, 则减半后加上优先值).
		for (p = &LAST_TASK; p > &FIRST_TASK; --p) {
			if (*p) {
				(*p)->counter = ((*p)->counter >> 1) + (*p)->priority; 	// 重置 counter 值.
			}
		}
	}
	// 用下面的宏(定义在 sched.h 中)把 current 指向任务号为 next 的任务, 并切换到该任务中运行. 
	// next 被初始化为 0. 因此若系统中没有任何其它任务处于就绪状态, 则 next 为 0. 
	// 因此调度函数会在系统空闲时去执行任务 0. 此时任务 0 仅执行 pause() 调用, 然后又会调用本函数(schedule).
	switch_to(next);					// 切换到任务项号为 next 的任务, 并运行之.
}

// pause() 系统调用. 转换当前任务的状态为可中断的等待状态, 并重新调度.
// 该系统调用将导致进程进入睡眠状态, 直到收到一个信号. 该信号用于终止进程或者使进程调用一个信号捕获函数. 
// 只有当捕获了一个信号, 并且信号捕获处理函数返回, pause() 才会返回. 
// 此时 pause() 返回值应该是 -1, 并且 errno 被置为 EINTR. 这里还没有完全实现(直到 0.95 版).
int sys_pause(void) {
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return 0;
}

// 把当前任务置为指定的睡眠状态(可中断的或不可中断的), 并让睡眠队列头指针指向当前任务. 
// 函数参数 p 是等待任务指针队列头指针. 指针是含有一个变量地址的变量.
// 这里参数 p 使用了指针的指针形式 '**p', 这是因为 **C 函数参数只能传值**, 
// 没有直接的方式让被调用函数改变调用该函数程序中变量的值. 
// 但是指针 '*p' 指向的目标(这里是任务结构)会改变, 
// 因此为了能修改调用该函数程序中原来就是指针变量的值, 就需要传递指针 '*p' 的指针, 即 '**p'.
// 参数 state 是任务睡眠使用的状态: TASK_INTERRUPTIBLE 或 TASK_INTERRUPTIBLE. 
// 处于不可中断睡眠状态(TASK_UNINTERRUPTIBLE)的任务需要内核程序利用 wake_up() 函数明确唤醒(比如在硬盘中断时调用). 
// 处于可中断睡眠状态(TASK_INTERRUPTIBLE)可以通过信号, 任务超时等手段唤醒(置为就绪状态 TASK_RUNNING).
// *** 注意, 由于本内核代码不是很成熟, 因此下列与睡眠相关的代码存在一些问题, 不宜深究.
static inline void __sleep_on(struct task_struct **p, int state) {
	struct task_struct *tmp;

	if (!p) return;												// 若指针无效, 则退出. 
		
	// 如果当前任务是任务 0, 则死机(impossible!).
	if (current == &(init_task.task)) {
		panic("task[0] trying to sleep");
	}
	// 让 tmp 指向在之前已经等待的任务(如果有的话), 
	// 然后将睡眠等待资源的任务指针重新指向当前任务(此时等待的任务指针是当前任务, 而不是之前的任务了, tmp 指向之前的任务). 
	// 这样就把当前任务指针放到了资源的等待任务指针池(p)中. 然后将当前任务置为指定的等待状态, 并执行重新调度.
	tmp = *p; 													// tmp 指向之前就已经在等待资源的任务(如果有的话).
	*p = current; 												// 更新睡眠等待资源的任务指针为当前任务指针.
	current->state = state; 									// 更新当前任务状态.
	// 将当前进程睡眠后立刻执行调度函数调度其它的进程执行.
repeat:	schedule();
	// 只有当任务被唤醒时, 程序才会返回到这里, 表示进程已被明确地唤醒(state 更新为 TASK_RUNNING)并执行. 
	// 这里要唤醒最后一个等待这个资源的任务, 如果当前资源不是最后一个等待资源的任务, 则继续睡眠等待, 并等着由最新的任务开始链式唤醒.
	// TODO: 这里有个问题, 这个 *p 不会被其它进程更新, 也就是说之前就已经等待的进程里再执行 (*p != current) 时永远成立, 也即永远不会被唤醒.
	if (*p && (*p != current)) { 								// 如果当前任务不是最后一个等待该资源的任务, 则唤醒最后的等待任务, 自己先进入睡眠状态.
		(**p).state = TASK_RUNNING; 							// 将最新等待资源的任务更新为就绪状态.
		current->state = TASK_UNINTERRUPTIBLE; 					// 当前任务先继续睡眠, 等待被唤醒.
		goto repeat;											// 重新调度, 让出 CPU 让其它进程先运行.
	}
	// 执行到这里, 说明本任务真正被唤醒执行. 
	// 此时等待队列头指针应该指向本任务, 若它为空, 则表明调度有问题, 于是显示警告信息. 
	// 如果在本任务之前就已有任务(tmp)在等待资源(tmp 不为空), 就一块唤醒它. 
	// 如果等待该资源的任务为空, 是有问题的, 不应该出现这种情况!
	if (!*p) { 													// 等待资源的任务为空是不对的!
		printk("Warning: current resource has no waitting task, *p = NULL\n\r");
	}
	// 把等待该资源的任务设置为在本任务之前就已经在等待的任务(如果有的话, 如果没有的话 tmp 就是 NULL, *p 也会变为 NULL), 并使这个任务就绪.
	if (*p = tmp) {
		// 如果在任务之前就已经有在等待的任务, 那么唤醒它: 链式唤醒毎一个之前就在等待该资源的任务.
		tmp->state = TASK_RUNNING;
	}
}

// 将当前任务置为可中断的等待状态(TASK_INIERRUPTIBLE), 并放入头指针 *p 指定的等待队列中.
void interruptible_sleep_on(struct task_struct ** p) {
	__sleep_on(p, TASK_INTERRUPTIBLE);
}

// 把当前任务置为不可中断的等待状态(TASK_UNINTERRUPTIBLE). 
// 并更新等待资源的任务为当前任务. 只有明确地被唤醒(wake_up())时才会被重新调度执行. 
// 参数 p 是资源(比如缓存块 buffer_head)结构体中的 wait 成员(比如 bh->b_wait)的地址, 该成员用于保存等待该资源的任务指针.
void sleep_on(struct task_struct ** p) {
	// 将进程设置为不可中断的等待状态, 此时进程不会被信号中断, 包括 KILL 信号.
	__sleep_on(p, TASK_UNINTERRUPTIBLE);
}

// 唤醒 *p(任务指针)指向的不可中断等待的任务. *p 是(最后进入等待队列)等待资源的任务指针. 
// 若该任务已经处于停止或僵死状态, 则显示警告信息.
// 参数 p 是资源(比如缓存块 buffer_head)结构体中的 wait 成员(比如 bh->b_wait)的地址, 该成员用于保存等待该资源的任务指针.
void wake_up(struct task_struct ** p) {
	if (p && *p) {
		if ((**p).state == TASK_STOPPED) {						// 处于停止状态.
			printk("wake_up: TASK_STOPPED");
		}
		if ((**p).state == TASK_ZOMBIE) {						// 处于僵死状态.
			printk("wake_up: TASK_ZOMBIE");
		}
		(**p).state = TASK_RUNNING;								// 置为就绪状态 TASK_RUNNING.
	}
}

/*
 * OK, here are some floppy things that shouldn't be in the kernel
 * proper. They are here because the floppy needs a timer, and this
 * was the easiest way of doing it.
 */
/*
 * 好了, 从这里开始是一些有关软盘的子程序, 本不应该放在内核的主要部分中的. 
 * 将它们放在这里是因为软驱需要定时处理, 而放在这里是最方便的.
 */
// 下面的数组 wait_motor[] 用于存放等待软驱马达启动到正常转速的进程指针. 
// 数组索引 0-3 分别对应软驱 A--D. 数组 mon_timer[] 存放各软驱马达启动所需要的滴答数.
// 程序中默认启动时间为 50 个滴答(0.5 秒). 
// 数组 moff_timer[] 存放各软驱在马达停转之前需维持的时间. 程序中设定为 1000 个滴答(100 秒).
static struct task_struct * wait_motor[4] = {NULL, NULL, NULL, NULL};
static int  mon_timer[4] = {0, 0, 0, 0};
static int moff_timer[4] = {0, 0, 0, 0};
// 下面变量对应软驱控制器中当前数字输出寄存器. 该寄存器每位定义如下:
// 位 7-4: 分别控制驱动器 D-A 马达的启动. 1 - 启动; 0 - 关闭.
// 位 3:1 - 允许 DMA 和中断请求; 0 - 禁止 DMA 和中断请求.
// 位 2:1 - 启动软盘控制器; 0 - 复位软盘控制器.
// 位 1-0: 00 - 11, 用于选择控制的软驱 A-D.
// 这里设置初值为: 允许 DMA 和中断请求, 启动 FDC.
unsigned char current_DOR = 0x0C;

// 指定软驱启动到正常运转状态所需等待时间.
// 参数 nr -- 软驱号(0--3), 返回值为滴答.
// 局部变量 selected 是选中软驱标志(blk_drv/floppy.c). 
// mask 是所选软驱对应的数字输出寄存器中启动马达位. 
// mask 高 4 位是各软驱启动马达标志.
int ticks_to_floppy_on(unsigned int nr) {
	extern unsigned char selected;
	unsigned char mask = 0x10 << nr;

	// 系统最多有 4 个软驱. 首先预先设置好指定软驱 nr 停转之前需要经过的时间(100 秒). 
	// 然后取当前 DOR 寄存器值到临时变量 mask 中, 并把指定软驱的马达启动标志置位.
	if (nr > 3) {
		panic("floppy_on: nr>3");
	}
	moff_timer[nr] = 10000;				/* 100 s = very big :-) */			// 停转维持时间.
	cli();								/* use floppy_off to turn it off */	// 关中断
	mask |= current_DOR;
	// 如果当前没有选择软驱, 则首先复位其他软驱的选择位, 然后指定软驱选择位.
	if (!selected) {
		mask &= 0xFC;
		mask |= nr;
	}
	// 如果数字输出寄存器的当前值与要求的值不同, 则向 FDC 数字输出端口输出新值(mask), 
	// 并且如果要求启动的马达还没有启动, 则置相应软驱的马达启动定时器值(HZ/2 = 0.5 秒或 50 个滴答). 
	// 若已经启动, 则再设置启动定时为 2 个滴答, 能满足下面 do_floppy_timer() 中先递减后判断的要求.
	// 执行本次定时代码的要求即可. 此后更新当前数字输出寄存器 current_DOR.
	if (mask != current_DOR) {
		outb(mask, FD_DOR);
		if ((mask ^ current_DOR) & 0xf0) {
			mon_timer[nr] = HZ / 2;
		} else if (mon_timer[nr] < 2) {
			mon_timer[nr] = 2;
		}
		current_DOR = mask;
	}
	sti();											// 开中断.
	return mon_timer[nr];							// 最后返回启动马达所需的时间值.
}

// 等待指定软驱马达启动所需的一段时间, 然后返回.
// 设置指定软驱的马达启动到正常转速所需的延时, 然后睡眠等待. 
// 在定时中断过程中会一直递减判断这里设定的延时值. 当延时到期, 就会执行这里的等待进程.
void floppy_on(unsigned int nr) {
	// 关中断. 如果马达启动定时还没到, 就一直把当前进程置为不可中断睡眠状态并放入等待马达运行的队列中. 然后开中断.
	cli();
	while (ticks_to_floppy_on(nr)) {
		sleep_on(nr + wait_motor);
	}
	sti();
}

// 置关闭相应软驱马达停转定时器(3 秒).
// 若不使用该函数明确关闭指定的软驱马达, 则在马达开启 100 秒之后也会被关闭.
void floppy_off(unsigned int nr) {
	moff_timer[nr] = 3 * HZ;
}

// 软盘定时处理子程序. 更新马达启动定时值和马达关闭停转时值. 
// 该子程序会在时钟定时中断过程中被调用, 因此系统每经过一个滴答(10ms)就会被调用一次, 随时更新马达开启或停转定时器的值. 
// 如果某一个马达停转定时到, 则将数字输出寄存器马达启动位复位.
void do_floppy_timer(void) {
	int i;
	unsigned char mask = 0x10;

	for (i = 0 ; i < 4 ; i++, mask <<= 1) {
		if (!(mask & current_DOR)) {					// 如果不是 DOR 指定的马达则跳过.
			continue;
		}
		if (mon_timer[i]) {								// 如果马达启动定时到则唤醒进程.
			if (!--mon_timer[i]) {
				wake_up(i + wait_motor);
			}
		} else if (!moff_timer[i]) {					// 如果马达停转定时到则复位相应马达启动位, 并且更新数字输出寄存器.
			current_DOR &= ~mask;
			outb(current_DOR, FD_DOR);
		} else {
			moff_timer[i]--;							// 否则马达停转计时递减.
		}
	}
}

// 下面是关于定时器的代码. 最多可有 64 个定时器.
#define TIME_REQUESTS 64

// 定时器链表结构和定时器数组. 该定时器链表专用于供软驱关闭马达和启动马达定时操作. 
// 这种类型定时器类似现代 Linux 系统中的动态定时器(Dynamic Timer), 仅供内核使用.
static struct timer_list {
	long jiffies;										// 定时滴答数.
	void (*fn)();										// 定时处理程序.
	struct timer_list * next;							// 链接指向下一个定时器.
} timer_list[TIME_REQUESTS], * next_timer = NULL;		// next_timer 是定时器队列头指针.

// 添加定时器. 输入参数为指定的定时值(滴答数)和相应的处理程序指针.
// 软盘驱动程序(floppy.c)利用该函数执行启动或关闭马达的延时操作.
// 参数 jiffies - 以10毫秒计的滴答数; *fn() - 定时时间到时执行的函数.
void add_timer(long jiffies, void (*fn)(void)) {
	struct timer_list * p;

	// 如果定时处理程序指针为空, 则退出. 否则关中断.
	if (!fn) return;
	cli();
	// 如果定时值 <= 0, 则立刻调用其处理程序. 并且该定时器不加入链表中.
	if (jiffies <= 0) {
		(fn)();
	} else {
		// 否则从定时器数组中, 找一个空闲项.
		for (p = timer_list; p < timer_list + TIME_REQUESTS; p++) {
			if (!p->fn) break;
		}
		// 如果已经用完了定时器数组, 则系统崩溃. 否则向定时器数据结构填入就信息, 并链入链表头.
		if (p >= timer_list + TIME_REQUESTS) {
			panic("No more time requests free");
		}
		p->fn = fn;
		p->jiffies = jiffies;
		p->next = next_timer;
		next_timer = p;
		// 链表项按定时值从小到大排序. 在排序时减去排在前面需要的滴答数, 这样在处理定时器时只要查看链表头的第一项的定时是否到期即可.
		// [[?? 这段程序好像没有考虑周全. 
		// 如果新插入的定时器值小于原来关一个定时器值时则根本没会进入循环中, 但此时还是应该将紧随后面的一个定时器值减去新的第 1 个的定时值. 
		// 即如果第 1 个定时值 <= 第 2 个, 则第 2 个定时值扣除第 1 个的值即可, 否则进入下面循环中进行处理.]]
		while (p->next && p->next->jiffies < p->jiffies) {
			p->jiffies -= p->next->jiffies;
			fn = p->fn;
			p->fn = p->next->fn;
			p->next->fn = fn;
			jiffies = p->jiffies;
			p->jiffies = p->next->jiffies;
			p->next->jiffies = jiffies;
			p = p->next;
		}
		// 修正上述问题.
		if(p->next && p->next->jiffies >= p->jiffies) {
			p->next->jiffies -= p->jiffies;
		}
	}
	sti();
}

// 时钟中断 C 函数处理程序, 在 sys_call.s 中的 timer_interrupt 被调用.
// 参数 cpl 是当前特权级 0 或 3, 是时钟中断发生时正被执行的代码选择符中的特权级. 
// cpl = 0 时表示中断发生时正在执行内核代码, cpl = 3 时表示中断发生时正在执行用户代码. 
// 对于一个进程由于执行时间片用完时, 则进行任务切换. 并执行一个计时更新工作.
void do_timer(long cpl) {
	static int blanked = 0; 						// 黑屏标志: 1 - 当前黑屏, 0 - 当前亮屏.

	// 如果 blankcount 倒计时(黑屏倒计时)不为零, 或者黑屏时间 blankinterval 为 0 的话, 表示此时屏幕应该是亮的.
	// 那么如果此时处于黑屏状态(黑屏标志 blanked=1)是不对的, 则让屏幕恢复显示. 
	// 然后倒计时值递减, 并将标志设置为亮屏状态.
	if (blankcount || !blankinterval) { 			// 如果黑屏时间结束或者没有设置黑屏时间.
		if (blanked) { 								// 如果当前是黑屏状态, 则唤醒屏幕.
			unblank_screen();						// 唤醒屏幕.
		}
		if (blankcount) { 							// 黑屏倒计时.
			blankcount--;
		}
		blanked = 0; 								// 当前是亮屏.
	// blankcount = 0 && blankinterval != 0 && blanked = 0: 如果设置了黑屏时间, 并且黑屏倒计时结束, 当前是亮屏状态, 那么需要关闭屏幕.
	} else if (!blanked) {
		blank_screen();
		blanked = 1;
	}
	// 接着处理硬盘操作超时问题. 如果硬盘超时计数递减之后为 0, 则进行硬盘访问超时处理.
	if (hd_timeout) {
		if (!--hd_timeout) {
			hd_times_out();							// 硬盘访问超时处理(blk_drv/hd.c).
		}
	}
	// 如果蜂鸣计时结束, 则关闭发声.(向 0x61 口发送命令, 复位位 0 和 1. 
	// 位 0 控制 8253 计数器 2 的工作, 位 1 控制扬声器.
	if (beepcount) {								// 扬声器发声时间滴答数(chr_drv/console.c)
		if (!--beepcount) {
			sysbeepstop();
		}
	}
	// 如果当前特权级(cpl)为 0, 表示进程被中断时在执行内核态代码, 则将进程的内核代码时间 stime 递增; 
	// 如果 cpl > 0, 则表示进程被中断时在执行用户态代码, 增加用户态代码执行时间 utime. 每 10ms 加 1.
	// [Linus 把内核程序统称为超级用户(superviser)的程序. 这种称呼来自 Intel CPU 手册.] 
	if (cpl) { 										// CPL != 0 表示被中断时进程在运行用户态代码.
		current->utime++;
	} else {										// 被中断时进程在运行内核态代码.
		current->stime++;
	}
	// 如果有定时器存在, 则将链表第 1 个定时器的值减 1. 如果已等于 0, 则调用相应的处理程序, 并将该处理程序指针置空.
	// 然后去掉该项定时器. next_timer 是定时器链表的头指针.
	if (next_timer) {
		next_timer->jiffies--;
		while (next_timer && next_timer->jiffies <= 0) {
			void (*fn)(void);						// 这里插入了一个函数指针定义!!!
			fn = next_timer->fn;
			next_timer->fn = NULL;
			next_timer = next_timer->next;
			(fn)();									// 调用定时处理函数.
		}
	}
	// 如果当前软盘控制器 FDC 的数字输出寄存器中马达启动位有置位的, 则执行软盘定时程序.
	if (current_DOR & 0xf0) {
		do_floppy_timer();
	}
	// 如果进程的 CPU 时间片还没用完, 则应该继续执行被中断进程, 退出. 否则置当前任务运行倒计时值为 0. 
	// 并且若发生时钟中断时正在内核态运行则返回, 否则调用执行调度函数.
	if ((--current->counter) > 0) return;
	current->counter = 0;
	if (!cpl) return;								// 对于内核态代码, 不根据 counter 值进行调度.
	schedule();
}

// 系统调用功能 - 设置报警定时时间值(秒).
// 若参数 seconds 大于 0, 则设置新定时值, 并返回原定时时刻还剩余的间隔时间. 否则返回 0.
// 进程数据结构中报警定时值ala)m的单位是系统滴答(1 滴答为 10 毫秒), 
// 它是系统开机起到设置定时操作时系统滴答值 jiffies 和转换成滴答单位的定时值之和, 即 'jiffies + HZ * 定时秒值'. 
// 而参数给出的是以秒为单位的定时值, 因此本函数的主要操作是进行两个单位的转换.
// 其中常数 HZ = 100, 是内核系统运行频率. 定义在 inlucde/sched.h 上.
// 参数 seconds 是新的定时时间值, 单位是秒.
int sys_alarm(long seconds) {
	int old = current->alarm;

	if (old) {
		old = (old - jiffies) / HZ;
	}
	current->alarm = (seconds>0)?(jiffies+HZ*seconds):0;
	return (old);
}

// 取当前进程号 pid
int sys_getpid(void) {
	return current->pid;
}

// 取父进程号 ppid
int sys_getppid(void) {
	return current->p_pptr->pid;
}

// 取用户 uid
int sys_getuid(void) {
	return current->uid;
}

// 取有效的用户号 euid
int sys_geteuid(void) {
	return current->euid;
}

// 取组号 gid
int sys_getgid(void) {
	return current->gid;
}

// 取有效的组号 egid
int sys_getegid(void) {
	return current->egid;
}

// 系统调用功能 -- 降低对 CPU 的使用优先权(有人会用吗?)
// 应该限制 increment 为大于 0 的值, 否则可使优先仅增大!!
int sys_nice(long increment) {
	if (current->priority - increment > 0) {
		current->priority -= increment;
	}
	return 0;
}

// 内核调度程序的初始化子程序.
void sched_init(void) {
	int i;
	struct desc_struct * p;										// 描述符表结构指针.

	// 由于 Linux 系统开发之初, 内核不成熟. 内核代码会被经常修改. 
	// Linus 怕无意中修改了这些关键性的数据结构, 造成与 POSIX 标准的不兼容. 
	// 这里加入下面这个判断语句并无必要, 纯粹是为了提醒自己以及其他修改内核代码的人.
	if (sizeof(struct sigaction) != 16) {						// sigaction 是存放有关信号状态的结构.
		panic("Struct sigaction MUST be 16 bytes");
	}
	// 在全局描述符表中设置任务 0 的任务状态段描述符(TSS)和局部数据表描述符(LDT).
	// FIRST_TSS_ENTRY 和 FIRST_LDT_ENTRY 的值分别是 4 和 5, 定义在 include/linux/sched.h 中. 
	// gdt 是一个描述符表数组(include/linux/head.h), 实际上对应程序 head.s 中的全局描述符表基址(gdt). 
	// 因此 gdt + FIRST_TSS_ENTRY 即为 gdt[FIRST_TSS_ENTRY](即是 gdt[4]), 即 gdt 数组第 4 项的地址.
	// 参见 include/asm/system.h
	set_tss_desc(gdt + FIRST_TSS_ENTRY, &(init_task.task.tss)); // 在 GDT 中设置任务 0 的任务状态段(TSS)描述符.
	set_ldt_desc(gdt + FIRST_LDT_ENTRY, &(init_task.task.ldt)); // 在 GDT 中设置任务 0 的局部描述符表(LDT)地址.
	// 清空任务数组和描述符表项(注意 i = 1 开始, 所以任务 0 的描述符还在). 描述符项结构定义在文件 include/linux/head.h 中.
	// 此处 p 指向 GDT 中的描述符 6(即 task1 的 tss, 从 0 开始).
	p = gdt + FIRST_TSS_ENTRY + 2; 	// gdt+6 -> 指向任务 1 的描述符: 0 - 没有用 null, 1 - 内核代码段 cs, 2 - 内核数据段 ds, 
									// 3 - 系统段 syscall, 4 - 任务状态段 TSS0, 5 - 局部表 LTD0, 6 - 任务状态段 TSS1 等.
	// 初始化除 task0 以外的其他进程指针及描述符表.
	for(i = 1; i < NR_TASKS; i++) {
		task[i] = NULL; 			// task0 已经初始化过了, 不需要再初始化, 此处从 task1 开始依次初始化每个任务的 tss 和 ldt.
		p->a = p->b = 0; 			// 初始化 tss_i. tss_i 中的 i 表示任务号.
		p++;
		p->a = p->b = 0; 			// 初始化 ldt_i. i 表示任务号.
		p++;
	}
	/* Clear NT, so that we won't have troubles with that later on */
	/* 清除标志寄存器中的位 NT, 这样以后就不会有麻烦. */
	// EFLAGS 中的 NT 标志位用于控制任务的嵌套调用. 
	// ** NOTE: 当 NT 置位时, 那么当前中断任务执行 IRET 指令时就会引起任务切换. **
	// NT 指出 TSS 中的 back_link 字段是否有效. NT = 0 时无效.
	__asm__("pushfl; andl $0xffffbfff, (%esp); popfl");
	// 将任务 0 的 TSS 段选择符加载到任务寄存器 tr. 
	// 将局部描述符表段选择符加载到局部描述符表寄存器 ldtr 中. 
	// 注意!! 是将 GDT 中相应 LDT 描述符的选择符加载到 ldtr. 
	// 		 只明确加载这一次, 以后新任务 LDT 的加载, 是 CPU 根据 TSS 中的 LDT 选择符自动加载.
	ltr(0);								// 将任务 0 的 tss 段选择符加载到任务寄存器 tr 中. (include/linux/sched.h)
	lldt(0);							// 将任务 0 的 LDT 段选择符加载到局部描述符表寄存器 LDTR 中. 其中参数(0)是任务号.
	// 下面代码用于初始化 8253 定时器. 通道 0, 选择工作方式 3, 二进制计数方式. 
	// 通道 0 的输出引脚接在中断控制主芯片的 IRQ0 上, 它每 10 毫秒发出一个 IRQ0 请求. LATCH 是初始定时计数值.
	outb_p(0x36, 0x43);					/* binary, mode 3, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff, 0x40);			/* LSB */							// 定时值低字节
	outb(LATCH >> 8, 0x40);				/* MSB */	 						// 定时值高字节
	// 设置时钟中断处理程序句柄(设置时钟中断门). 修改中断控制器屏蔽码, 允许时钟中断.
	// 然后设置系统调用中断门. 这两个设置中断描述符表 IDT 中描述符的宏定义在文件 include/asm/system.h 中. 
	// 两者的区别参见 system.h 文件开始处的说明.
	set_intr_gate(0x20, &timer_interrupt); 		// timer_interrupt 在 kernel/sys_call.s 中.
	outb(inb_p(0x21) & ~0x01, 0x21);
    // int 0x80 中断是陷阱门, 陷阱门属于调用门, 描述符中含有中断/异常处理程序的段选择符.
    // 用户代码调用中断时(int 0x80)时, 会通过这个选择符来定位到中断/异常处理程序(用户态代码可以使用这个门来实现对系统内核代码的调用).
	// 当 CPL = 3 的用户态代码调用 system_call 中断时, 会发生特权级变化, 导致堆栈切换.
    // 重点看 Chapter 4.5.3.3; system_call 在 kernel/sys_call.s 中.
	set_system_gate(0x80, &system_call); 		// (系统调用(陷阱)门 DPL = 3, 即用户态的代码可以调用陷阱门来实现对系统代码的调用) 
}
