/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
/*
 * 'fork.c' 中含有系统调用 'fork' 的辅助子程序, 以及一些其他函数('verify_area'). 
 * 一旦你了解了 fork, 就会发现它非常简单的, 但内存管理却有些难度. 
 * 参见 'mm/memory.c' 中的 'copy_page_tables()' 函数.
 */
#include <errno.h>							// 错误号头文件. 包含系统中各种出错号.

#include <linux/sched.h>					// 调试程序头文件, 定义了任务结构 task_struct, 任务 0 的数据.
#include <linux/kernel.h>
//#include <asm/segment.h>
#include <asm/system.h>

// 写页面验证. 若页面不可写, 则复制页面. 定义在 mm/memory.c 中.
extern void write_verify(unsigned long address);

long last_pid = 0;							// 最新进程号, 其值会由 get_empty_process() 生成.

// 进程空间区域写前验证函数.
// 对于 80386 CPU, 在执行特权级 0 代码时不会理会用户空间中的页面是否是页保护的, 
// 因此在执行内核代码时用户空间中数据页面保护标志起不了作用, 写时复制机制也就失去了作用. 
// verify_area() 函数就用于此目的. 但对于 80486 或后来的 CPU, 其控制寄存器 CR0 中有一个写保护标志 WP(位 16), 
// 内核可以通过设置该标志来禁止特权级 0 的代码向用户空间只读页面执行写数据, 否则将导致发生写保护异常. 
// 从而 486 以上 CPU 可以通过设置该标志来达到使用本函数同样的目的. 
// 该函数对当前进程逻辑地址从 addr 到 addr + size 这一段范围以页为单位执行写操作前的检测操作. 
// 由于检测判断是以页面为单位进行操作, 因此程序首先需要找出 addr 所在页面开始地址 start, 然后 start 加上进程数据段基址,
// 使这个 start 变换成 CPU 4GB 线性空间中的地址. 最后循环调用 write_verify() 对指定大小的内存空间进行写前验证. 
// 若页面是只读的, 则执行共享检验和复制页面操作(写时复制).
void verify_area(void * addr, int size) {
	unsigned long start;

	// 首先将起始地址 start 调整为其所在页的左边界开始位置, 同时相应地调整验证区域大小. 
	// 下句中的 start & 0xfff 用来获得指定起始位置 addr(也即 start) 在所在页面中的偏移值, 
	// 原验证范围 size 加上这个偏移值即扩展成以 addr 所在页面起始位置开始的范围值. 
	// 因此在 30 行上也需要把验证开始位置 start 调整成页面边界值.
	start = (unsigned long)addr;
	size += start & 0xfff;
	start &= 0xfffff000;					// 此时 start 是当前进程空间中的逻辑地址.
	// 下面把 start 加上进程数据段在线性地址空间中的起始基址, 变成系统整个线性空间中的地址位置. 
	// 对于 Linux 0.1x 内核, 其数据段和代码段在线性地址空间中的基址和限长均相同. 
	// 然后循环进行写页面验证. 若页面不可写, 则复制页面. (mm/memory.c)
	start += get_base(current->ldt[2]);
	while (size > 0) {
		size -= 4096;
		write_verify(start);				// include/linux/sched.h
		start += 4096;
	}
}

// 复制内存页表.
// 参数 nr 是新任务号; p 是新任务数据结构指针. 该函数为新任务在线性地址空间中设置代码段和数据段基址, 并复制页表. 
// 由于 Linux 系统采用写时复制(copy on write)技术, 因此这里仅为新进程设置自己的页目录表项和页表项, 
// 而没有实际为新进程分配物理内存页面. 此时新进程与其父进程共享所有内存页面. 操作成功返回 0, 否则返回出错号.
int copy_mem(int nr, struct task_struct * p)
{
	unsigned long old_data_base, new_data_base, data_limit;
	unsigned long old_code_base, new_code_base, code_limit;

	// 首先取当前进程(父进程)局部描述符表(LDT)中代码段描述符和数据段描述符项中的段限长(字节数).
	// 0x0f 是局部代码段选择符; 0x17 是局部数据段选择符. 然后取当前进程代码段和数据段的线性地址空间中的基地址. 
	// 由于 Linux-0.12 内核还不支持代码段和数据段隔离的情况, 因此这里需要检查代码段和数据段基址是否都相同, 
	// 并且要求数据段的长度至少不小于代码段的长度, 否则内核显示出错信息, 并停止运行.
	// get_limit() 和 get_base() 定义在 include/linux/sched.h.
	code_limit = get_limit(0x0f); 							// 0x0f = 0b-00001-1-11 (LDT 表项 1[从 0 开始] 局部代码段, 特权级 3)
	data_limit = get_limit(0x17); 							// 父进程(当前进程)的数据段长度: 0x17 = 0b-00010-1-11 (LDT 表项 2, 局部数据段, 特权级 3)
	old_code_base = get_base(current->ldt[1]); 				// 这里的 ldt 是 task_struct 结构中的 ldt 字段, 即当前任务的局部描述符表
	old_data_base = get_base(current->ldt[2]); 				// 获取当前进程(父进程)的代码段和数据段基地址.
	if (old_data_base != old_code_base) {
		panic("We don't support separate I&D");
	}
	if (data_limit < code_limit) {
		panic("Bad data_limit");
	}
	// 然后设置新进程在线性地址空间中的基地址等于(64MB * 其任务号[nr]), 并用该值设置新进程局部描述符表(LDT)中代码段和数据段描述符中的基地址. 
	// 接着设置新进程的页目录表项和页表项, 即复制当前进程(父进程)的页目录表项和页表项. 此时子进程共享父进程的内存页面.
	// 正常情况下 copy_page_tables() 返回 0, 否则表示出错, 则释放刚申请的页表项.
	new_data_base = new_code_base = nr * TASK_SIZE; 		// ** 任务的段基地址 = 任务号 * 64MB ** (线性地址)
	p->start_code = new_code_base; 							// nr = 1 时, start_code = 64 * 1024 * 1024Byte(设置代码段基地址).
	// 不同进程设置不同的段基地址是进程之间内存隔离的第一个手段, 比如任务 0 的段基地址是 0x0, 任务 1 的段基地址是 64MB, 
	// 那么任务 0 访问 0x01 时, 转换为线性地址就是 0x01, 任务 1 访问 0x01 时, 转换为线性地址 64MB + 0x01, 然后再经过不同的页转换为实际的物理内存地址.
	// 由于此处在 LDT 中设置的段基地址等同于线性地址(所有进程共用同一个页目录表), 所以在线性地址到物理地址转换时, 
	// 该进程的段基地址就是一个很大的地址, 在查找对应的页目录项时就会定位到该进程对应的页目录项!!!
	// 以 TASK-1 举例, new_code_base == 67108864 = 64MB, 所以在定位页目录项时, 
	// 起始页目录项就是 0x4(64MB >> 22 = 16[目录项所在地址] / 4[每个目录项占 4 字节] = 4[项号]), 即页目录表中第五项.
	// TODO: 我觉得对于不同的进程可以使用不同的 cr3 值来实现不共用同一张页目录表, 
	// 比如 TASK-0 的 cr3 = 0x0, TASK-1 的 cr3 = 0x4, 这样可以使用不同的页目录表, 从而实现相同不同进程使用相同的线性基地址.
	set_base(p->ldt[1], new_code_base); 					// 因为 Linux-0.12 中所有进程共用同一个页目录表, 所以, 共用同一个大的线性地址(4GB).
	set_base(p->ldt[2], new_data_base);
	if (copy_page_tables(old_data_base, new_data_base, data_limit)) {
		free_page_tables(new_data_base, data_limit);
		return -ENOMEM;
	}
	return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
/*
 * OK, 下面是主要的 fork 子程序. 它复制系统进程信息(task[n]), 并且设置必要的寄存器. 它还整个地复制数据段(也是代码段).
 */
// 复制进程.
// 该函数的参数是进入系统调用中断处理过程(sys_call.s)开始, 
// 直至调用本系统调用处理过程前逐步压入进程内核态栈的各寄存器的值.
// 这些在 sys_call.s 程序中逐步压入内核栈的值(参数)包括:
// 1. CPU执行中断指令时发生堆栈切换时自动压入的用户态的栈地址 ss 和 esp, 标志 eflags 和返回地址 cs 和 eip;
// 		在执行中断处理过程时如果发生特权级变化, int 0x80 指令会依次入栈: ss, esp, eflags, cs, eip(图 4-29)
// 2. 在刚进入 system_call 时入栈的段寄存器 ds, es, fs 和 edx, ecx, ebx;
// 3. 调用 sys_call_table 中 sys_fork 函数入栈的返回地址(参数 none 表示); system_call 中的 call *%ebx 入栈了 call 指令返回时的地址.
// 4. 调用 copy_process() 之前入栈的 gs, esi, edi, ebp 和 eax(nr).
// 其中参数 nr 是调用 find_empty_process() 分配的任务数组项号.
int copy_process(int nr, long ebp, long edi, long esi, long gs,  		// 这几个参数在 sys_fork 程序中手动压入栈中.
		long none, 														// system_call 中的 `call *%ebx` 指令入栈了该数据(下一行代码 `pushl %eax` 的地址).
		long ebx, long ecx, long edx, long orig_eax, 					// system_call 中入栈的数据
		long fs, long es, long ds, 										// system_call 压入的段寄存器及通用寄存器
		long eip, long cs, long eflags, long esp, long ss) {			// 用户态任务调用 int 0x80 中断时, cpu 自动压入栈中的用户态下的 ss, esp, eflags, cs, eip

	struct task_struct * p;
	int i;
	struct file * f;

	// 首先为新任务结构体数据分配内存. 如果内存分配出错, 则返回出错码并退出. 
	// 然后将新任务结构指针放入任务数组的 nr 项中. 
	// 其中 nr 为任务号, 由前面 find_empty_process() 返回. 
	// 接着把当前进程任务结构复制到刚申请到的物理内存页面 p 开始处(p 指向新申请的空闲物理页面地址).
	p = (struct task_struct *)get_free_page(); 		// 获取空闲的页面的物理起始地址(mem_map[i] 中标记为未使用项所对应页面的物理地址).
	if (!p) return -EAGAIN; 						// 如果申请到的空闲页面的物理地址为 0x0, 则表示申请失败, 返回出错码.
		
	task[nr] = p; 									// 任务项指向刚申请的进程物理内存地址; task_struct * task[NR_TASKS] 定义在 kernel/sched.c 中.
	// 复制当前任务(也是新进程的父进程)体信息, 作为新进程的初始信息
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */	/* 注意! 这样不会复制超级用户堆栈(只复制当前进程的属性信息) */
	// 随后对复制来的进程结构内容进行一些修改, 作为新进程的任务结构. 
	// 先将新进程的状态置为不可中断等待状态, 以防止内核调度其执行. 
	// 然后设置新进程的进程号为之前申请的 last_pid, 并初始化进程运行时间片值等于其 priorty 值(一般为 16 个嘀嗒).
	// 接着复位新进程的信号位图, 报警定时值, 会话(session)领导标志 leader, 进程及其子进程在内核和用户态运行时间统计值等. 
	p->state = TASK_UNINTERRUPTIBLE; 		// 防止时钟中断切换到该进程执行. 参见: (kernel/sched.c#schedule 方法)
	p->pid = last_pid;						// 设置新进程号(last_pid 为全局变量). 由 find_empty_process() 得到.
	p->counter = p->priority;				// 运行时间片值(单位: 嘀嗒数)(越大运行时间越长).
	p->signal = 0;							// 信号位图.
	p->alarm = 0;							// 报警定时值(嘀嗒数).
	p->leader = 0;							/* process leadership doesn't inherit */	/* 进程的领导权是不能继承的 */
	p->utime = p->stime = 0;				// 用户态总运行时间和内核态总运行时间.
	p->cutime = p->cstime = 0;				// 子进程用户态和内核态运行时间.
	p->start_time = jiffies;				// 进程开始运行时的时间(当前开机时间的滴答数 [每 10ms/滴答]).
	// 再设置任务状态段 TSS 中的数据. 
	// 系统给任务结构 p 分配了 1 页新内存, 让 ss0:esp0(程序的内核态堆栈)指向该页末端(PAGE_SIZE + (long) p). 
	p->tss.back_link = 0;
	p->tss.esp0 = (long) p + PAGE_SIZE;		// 任务内核态栈指针(指向该任务结构体所在物理内存页面的末端处). p -->|task_struct| ------- |<-- ss0:esp0
	p->tss.ss0 = 0x10;              		// 内核态栈的段选择符(内核数据段).
	// **即新任务开始运行时会从父进程调用 `int $0x80 sys_fork` 的下一条指令开始运行**
	p->tss.eip = eip;						// **设置新进程的指令代码指针指向父进程进行系统调用时的下一行代码.**
	p->tss.eflags = eflags;					// 标志寄存器.
	p->tss.eax = 0;							// **这是当 fork() 返回时新进程会返回 0 的原因所在**
	p->tss.ecx = ecx; 						// 以下几个寄存器是由参数传入.
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp; 						// 此时还是用的父进程的堆栈.
	p->tss.ebp = ebp; 						// 这些寄存器信息(动态字段)因为更新后还没有保存到父进程的 TSS 里,
	p->tss.esi = esi; 						// 复制进程信息时这些值还没有更新(会在进程切换时更新), 所以这里直接设置一下
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;				// 段寄存器仅 16 位有效.
	p->tss.cs = cs & 0xffff;				// cs = 0xf = 0b-1111(LDT 中的代码段).
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	// 每个任务在 GDT 表中都有两个段描述符: 任务的 TSS 段描述符和任务的 LDT 段描述符.
	// 更新该任务 TSS 段中的 LDT 选择符(用于选择该任务在 GDT 中的 LDT 段描述符).
	// 当执行切换任务时, CPU 会自动从 TSS 段中把任务的 LDT 段选择符加载到 ldtr 寄存器中.
	p->tss.ldt = _LDT(nr);					// 任务局部段描述符表的段选择符(LDT 描述符在 GDT 中).
	p->tss.trace_bitmap = 0x80000000;		// (高 16 位有效).
	// 如果当前任务使用了协处理器, 就保存其上下文. 汇编指令 clts 用于清除控制寄存器 CR0 中的任务已交换(TS)标志. 
	// 每当发生任务切换, CPU 都会设置该标志. 该标志用于管理数学协处理器: 如果该标志置位, 那么每个 ESC 指令都会被捕获(异常 7). 
	// 如果协处理器存在标志 MP 也同时置位的话, 那么 WAIT 指令也会捕获. 
	// 因此, 如果任务切换发生在一个 ESC 指令开始执行之后, 则协处理器中的内容就可能需要在执行新的 ESC 指令之前保存起来. 
	// 捕获处理句柄会保存协处理器的内容并复位 TS 标志. 
	// 指令 fnsave 用于把协处理器的所有状态保存到目的操作数指定的内存区域中(tss.i387).
	if (last_task_used_math == current) {
		__asm__("clts; fnsave %0; frstor %0" : : "m" (p->tss.i387));
	}

	// 设置新任务 LDT 代码段和数据段描述符中的段基地址(未设置段限长, 段限长在 do_execve 时设置), 并复制页表. 
	// 如果出错(返回值不是 0), 则复位任务数组中相应项并释放为该新任务分配的用于任务结构的内存页.
	if (copy_mem(nr, p)) {					// 返回不为 0 表示出错.
		task[nr] = NULL;
		free_page((long) p);
		return -EAGAIN;
	}
	// 如果父进程中有文件是打开的, 则将对应文件的打开次数增 1. 因为这里创建的子进程会与父进程共享这些打开的文件. 
	// 将当前进程(父进程)的 pwd, root, executable, library 引用次数均增 1. 与上面同样的道理, 子进程也引用了这些 i 节点.
	for (i = 0; i < NR_OPEN; i++) {
		if (f = p->filp[i]) {
			f->f_count++;
		}
	}
	if (current->pwd) {
		current->pwd->i_count++;
	}
	if (current->root) {
		current->root->i_count++;
	}
	if (current->executable) {				// 进程对应的可执行文件的 inode 指针
		current->executable->i_count++;
	}
	if (current->library) {
		current->library->i_count++;
	}
	// 随后在 GDT 表中设置新任务 TSS 段和 LDT 段描述符项. 这两个段的限长均被设置成 104 字节.
	// 参见 include/asm/system.h. 然后设置进程之间的关系链表指针, 即把新进程插入到当前进程的子进程链表中. 
	// 把新进程的父进程设置为当前进程, 把新进程的最新子进程指针 p_cpt 和年轻兄弟进程指针 p_ysptr 置空. 
	// 接着让新进程的老兄进程指针 p_osptr 设置等于父进程的最新子进程指针. 
	// 若当前进程确实还有其他子进程, 则让比邻老兄进程的最年轻进程指针 p_yspter 指向新进程. 
	// 最后把当前进程的最新子进程指针指向这个新进程. 然后把新进程设置成就绪态. 最后返回新进程号.
	// 另外, set_tss_desc() 和 set_ldt_desc() 定义在 include/asm/system.h 文件中. 
	// "gdt + (nr << 1) + FIRST_TSS_ENTRY" 是任务 nr 的 TSS 描述符项在全局表中的地址.
	// 因为每个任务占用 GDT 表中 2 项, 因此上式中要包括 '(nr << 1)'.
	// 请注意, 在任务切换时, 任务寄存器 tr 会由 CPU 自动加载.
	set_tss_desc(gdt + (nr << 1) + FIRST_TSS_ENTRY, &(p->tss)); 		// 在 GDT 中设置新进程的 tss 和 ldt 描述符.
	set_ldt_desc(gdt + (nr << 1) + FIRST_LDT_ENTRY, &(p->ldt));
	p->p_pptr = current;				// 设置新进程的父进程指针.
	p->p_cptr = 0;						// 复位新进程的最新子进程指针.
	p->p_ysptr = 0;						// 复位新进程的比邻年轻兄弟进程指针.
	p->p_osptr = current->p_cptr;		// 设置新进程的比邻老兄兄弟进程指针.
	if (p->p_osptr)	{					// 若新进程有老兄兄弟进程, 则让其年轻进程兄弟指针指向新进程
		p->p_osptr->p_ysptr = p;
	}
	current->p_cptr = p;				// 让当前进程最新子进程指针指向新进程.
	p->state = TASK_RUNNING;			/* do this last, just in case */  /* 设置进程状态为待运行状态 */
	// Log(LOG_INFO_TYPE, "<<<<< fork new process current_pid = %d, child_pid = %d, nr = %d >>>>>\n", current->pid, p->pid, nr);
	return last_pid;        			// 返回新进程号.
}

// 为新进程取得不重复的进程号 last_pid(并不需要返回这个 last_pid, 因为它是全局变量, 而是只返回任务号).
// 函数返回在任务数组中的任务号(数组项). 注意: last_pid != 任务号.
// last_pid 是进程号, 全局变量，每次调用该函数都**记录**一个未被占用的进程号(last_pid), 
// 并返回一个空闲的任务项号(task[i] 中的 i).
int find_empty_process(void) {
	int i;

	// 首先获取新的进程号. 如果 last_pid 增加 1 后超出进程号的正数表示范围, 则重新从 1 开始使用 pid 号. 
	// 然后在任务数组(task[])中搜索刚生成的 pid 号是否已经被某个任务使用. 
	// 如果是则跳转到函数开始处重新生成一个 pid 号. 
	// 如果找到一个没被占用的 pid 号, 则接着在任务数组中为新任务寻找一个空闲项, 并返回项号. 
	// last_pid 是一个全局变量, 不用作为返回值返回. 如果此时任务数组中 64 个项已经被全部占用, 则返回出错码.
	repeat:
		if ((++last_pid) < 0) {
			last_pid = 1;
		}
		for (i = 0; i < NR_TASKS; i++) {
			// 如果当前测试的 pid(last_pid) 已经被占用, 则重新生成一个 last_pid.
			if (task[i] && ((task[i]->pid == last_pid) || (task[i]->pgrp == last_pid)))  {
				goto repeat; 			// 重新生成一个 last_pid.
			}
		}
	// 如果找到一个未被占用的进程号, 则查找任务数组中的空闲项. 
	for (i = 1; i < NR_TASKS; i++) {
		if (!task[i]) {					// 如果找到一个空闲的任务项.
			return i; 					// 返回空闲的任务项号.
		}
	}
	// 如果没有找到空闲项, 则返回出错码.
	return -EAGAIN;
}
