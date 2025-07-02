/*
 *  linux/kernel/signal.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>
//#include <linux/kernel.h>
#include <asm/segment.h>

#include <signal.h>
#include <errno.h>

// 获取当前任务信号屏蔽位图(屏蔽码或阻塞码). sgetmask 可分解为 signal-get-mask. 以下类似.
int sys_sgetmask() {
	return current->blocked;
}

// 设置新的信号屏蔽位图. 信号 SIGKILL 和 SIGSTOP 不能被屏蔽. 返回值是原信号屏蔽位图.
int sys_ssetmask(int newmask) {
	int old = current->blocked;

	current->blocked = newmask & ~(1 << (SIGKILL - 1)) & ~(1 << (SIGSTOP - 1));
	return old;
}

// 检测并取得进程收到的但被屏蔽(阻塞)的信号. 还未处理信号的位图将被放入 set 中.
int sys_sigpending(sigset_t *set) {
    /* fill in "set" with signals pending but blocked. */
    /* 用还未处理并且被阻塞信号的位图填入 set 指针所指位置处 */
	// 首先验证进程提供的用户存储空间就有 4 个字节. 然后把还未处理并且被阻塞信号的位图填入 set 指针所指位置处.
    verify_area(set, 4);
    put_fs_long(current->blocked & current->signal, (unsigned long *)set);
    return 0;
}

/* atomically swap in the new signal mask, and wait for a signal.
 *
 * we need to play some games with syscall restarting.  We get help
 * from the syscall library interface.  Note that we need to coordinate
 * the calling convention with the libc routine.
 *
 * "set" is just the sigmask as described in 1003.1-1988, 3.3.7.
 * 	It is assumed that sigset_t can be passed as a 32 bit quantity.
 *
 * "restart" holds a restart indication.  If it's non-zero, then we
 * 	install the old mask, and return normally.  If it's zero, we store
 * 	the current mask in old_mask and block until a signal comes in.
 */
/*
 * 自动地更换成新的信号屏蔽码, 并等待信号的到来.
 *
 * 我们需要对系统调用(syscall)做一些处理. 我们会从系统调用库接口取得某些信息. 注意, 我们需要把调用规则与 libc 库
 * 中的子程序统一考虑.
 *
 * "set" 正是 POSIX 标准 1003.1-1998 的 3.3.7 节中所描述的信号屏蔽码 sigmask. 
 * 其中认为类型 sigset_t 能够作为一个 32 位量传递。
 *
 * "restart" 中保持有重启标志. 如果为非 0 值, 那么我们就设置原来的屏蔽码, 并且正常返回. 
 * 如果它为 0, 那么我们就把当前的屏蔽码保存在 oldmask 中并且阻塞进程, 直到收到任何一个信号为止.
 */
// 该系统调用临时把进程信号屏蔽码替换成参数中给定的 set, 然后挂起进程, 直到收到一个信号为止.
// restart 是一个被中断的系统调用重新启动标志. 当第 1 次调用该系统调用时, 这是 0. 
// 并且在该函数中会把进程原来的阻塞码 blocked 保存起来(old_mask), 并设置 restart 为非 0 值. 
// 因此当进程第 2 次调用该系统调用时, 它就会恢复进程原来保存在 old_mask 中的阻塞码.
int sys_sigsuspend(int restart, unsigned long old_mask, unsigned long set) {
	// pause()系统调用将导致调用它的进程进入睡眠状态, 直到收到一个信号. 
	// 该信号或者会终止进程时执行, 或者导致进程去执行相应的信号捕获函数.
    extern int sys_pause(void);

	// 如果 restart 标志不为 0, 表示重新让程序运行起来. 于是恢复前面保存在 old_mask 中的原进程阻塞码. 
	// 并返回码 -EINTR(系统调用被信号中断).
    if (restart) {
			/* we're restarting */  /* 我们正在重新启动系统调用 */
			current->blocked = old_mask;
			return -EINTR;
    }
	// 否则表示 restart 标志的值是 0. 表示第 1 次调用. 于是首先设置 restart 标志(置为 1), 
	// 保存进程当前阻塞码 blocked 到 old_mask 中, 并把进程的阻塞码替换成 set. 
	// 然后调用 pause() 让进程睡眠, 等待信号的到来. 当进程收到一个信号时, pause()就会返回, 
	// 并且进程会去执行信号处理函数, 然后本调用返回 -ERESTARTNOINTR 码退出. 
	// 这个返回码说明在处理完信号后要求返回到本系统调用中继续运行, 即本系统调用不会被中断.
    /* we're not restarting.  do the work */
    /* 我们不是重新运行, 那么就干活吧 */
    //*(&restart) = 1;
	__asm__("movl $1, %0\n\t" \
			: : "m"(restart));
    //*(&old_mask) = current->blocked;
	__asm__("movl %%eax, %0\n\t" \
			: : "m"(old_mask), "a"(current->blocked));
    current->blocked = set;
    (void) sys_pause();			/* return after a signal arrives */
    return -ERESTARTNOINTR;		/* handle the signal, and come back */
}

// 复制 sigaction 数据到 fs 数据段 to 处. 即从内核空间复制到用户(任务)数据段中.
static inline void save_old(char * from, char * to) {
	int i;

	// 首先验证 to 处的内存空间是否足够大. 然后把一个 sigaction 结构信息复制到 fs 段(用户)空间中. 
	// 宏函数 put_fs_byte() 在 include/asm/segment.h 中实现.
	verify_area(to, sizeof(struct sigaction));
	for (i = 0; i < sizeof(struct sigaction); i++) {
		put_fs_byte(*from, to);
		from++;
		to++;
	}
}

// 把 sigaction 数据从 fs 数据段 from 位置复制到 to 处. 即从用户数据空间取到内核数据段中.
static inline void get_new(char * from, char * to) {
	int i;

	for (i = 0; i < sizeof(struct sigaction); i++) {
		*(to++) = get_fs_byte(from++);
	}
}

// signal()系统调用. 类似于 sigaction(). 为指定的信号安装新的信号句柄(信号处理程序). 
// 信号句柄可以是用户指定的函数, 也可以是 SIG_DFL(默认句柄)或 SIG_IGN(忽略). 
// 参数 signum -- 指定的信号; handler -- 指定的句柄; restorer -- 恢复函数指针, 该函数由 Libc 库提供. 
// 用于在信号处理程序结束后恢复系统调用返回时几个寄存器的原有值以及系统调用的返回值, 
// 就好像系统调用没有执行过信号处理程序而直接返回到用户程序一样. 函数返回原信号句柄. 
int sys_signal(int signum, long handler, long restorer) {
	struct sigaction tmp;

	// 首先验证信号值在有效范围(1--32)内, 并且不得是信号 SIGKILL 和 SIGSTOP. 因为这两个信号不能被进程捕获. 
	if (signum < 1 || signum > 32 || signum == SIGKILL || signum == SIGSTOP) {
		return -EINVAL;
	}
	// 然后根据提供的参数组建 sigaction 结构内容. sa_handler 是指定的信号处理句柄(函数). 
	// sa_mask 是执行信号处理句柄时的信号屏蔽码. sa_flags 是执行时的一些标志组合. 
	// 这里设定该信号处理句柄只使用 1 次后就恢复到默认值, 并允许信号在自己的处理句柄中收到. 
	tmp.sa_handler = (void (*)(int))handler;
	tmp.sa_mask = 0;
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK;
	tmp.sa_restorer = (void (*)(void))restorer;    				// 保存恢复处理函数指针. 
	// 接着取该信号原来的处理句柄, 并设置该信号的 sigaction 结构. 最后返回原信号句柄. 
	handler = (long)current->sigaction[signum - 1].sa_handler;
	current->sigaction[signum - 1] = tmp;
	return handler;
}

// sigaction()系统调用. 改变进程在收到一个信号时的操作. signum 是除了 SIGKILL 以外的任何信号. 
// [如果新操作(action)不为空] 则新操作被安装. 如果 oldaction 指针不为空, 则原操作被保留到 oldaction. 
// 成功则返回 0, 否则为 -EINVAL. 
int sys_sigaction(int signum, const struct sigaction * action, struct sigaction * oldaction) {
	struct sigaction tmp;

	// 首先验证信号值在有效范围(1--32)内, 并且不得是信号 SIGKILL 和 SIGSTOP. 因为这两个信号不能被进程捕获. 
	if (signum < 1 || signum > 32 || signum == SIGKILL || signum == SIGSTOP) {
		return -EINVAL;
	}
	// 在信号的 sigaction 结构中设置新的操作(动作). 如果 oldaction 指针不为空的话, 则将原操作指针保存到 oldaction 所指的位置. 
	tmp = current->sigaction[signum - 1];
	get_new((char *)action, (char *)(signum - 1 + current->sigaction));
	if (oldaction) {
		save_old((char *) &tmp,(char *) oldaction);
	}
	// 如果允许信号在自己的信号句柄中收到, 则令屏蔽码为 0, 否则设置屏蔽本信号. 
	if (current->sigaction[signum - 1].sa_flags & SA_NOMASK) {
		current->sigaction[signum - 1].sa_mask = 0;
	} else {
		current->sigaction[signum - 1].sa_mask |= (1 << (signum - 1));
	}
	return 0;
}

/*
 * Routine writes a core dump image in the current directory.
 * Currently not implemented.
 */
/*
 * 在当前目录中产生 core dump 映像文件的子程序. 目前还没有实现. 
 */
int core_dump(long signr) {
	return(0);	/* We didn't do a dump */
}

// 系统调用或时钟中断的中断返回处理程序中真正的信号预处理程序(在 kernel/sys_call.s). 
// 这段代码的主要作用是将信号处理程序句柄插入到用户程序堆栈中, 并通过替换中断返回时的地址(指向信号处理程序), 
// 实现在系统调用或中断返回后立刻执行信号处理程序, 处理完信号后继续执行用户的程序. 
// 函数的参数是进入系统调用处理程序 sys_call.s 开始, 直到调用本函数前逐步压入堆栈的值. 
// 这些值包括：
// 1. CPU 执行中断指令压入的用户栈地址 ss, esp(esp 和 ss 在特权级没发生变化时不会压入栈中), eflags, cs, eip; (见图 4-29)
// 2. 刚进入 system_call 时压入栈的段寄存器 ds, es, fs, eax(orig_eax, 系统调用的功能号, 本次调用如果是由时钟中断返回时调用, 则该值为 -1), edx, ecx, ebx;
// 3. system_call 压入栈中的 sys_call_tables 中的系统调用函数的返回值(eax). 
// 4. 执行本函数前压入栈中的当前处理的信号值(signr). 
int do_signal(long signr, long eax,  											// signr 由 ret_from_sys_call 入栈, eax 是系统调用返回值或时钟中断中入栈.
	long ebx, long ecx, long edx, long orig_eax, long fs, long es, long ds, 	// system_call 执行系统调用前或 timer_interrupt 中断压入栈中的参数.
	long eip, long cs, long eflags, unsigned long * esp, long ss) {				// 执行中断时压入栈中的参数(如果中断时没有特权级变化, 则不会压入 esp 和 ss). 
																				// 所以如果判断被中断时处于内核态, 则不应该使用这两个参数.
	unsigned long sa_handler;
	long old_eip = eip;
	struct sigaction * sa = current->sigaction + signr - 1;			// 得到当前进程对应信号的处理行为.
	int longs;                      								// 即 current->sigaction[signr - 1]. 

	unsigned long * tmp_esp;

// 以下是调试语句. 当定义了 notdef 时会打印相关信息. 
#ifdef notdef
	printk("pid: %d, signr: %d, eax = %d, oeax = %d, int = %d\n", current->pid, signr, eax, orig_eax, sa->sa_flags & SA_INTERRUPT);
#endif	/* Continue, execute handler */
	// 如果不是系统调用而是其他中断(比如时钟中断)执行中断返回时调用的本函数, 则 orig_eax 值为 -1. 
	// 因此当 orig_eax 不等于 -1 时, 说明是在某个系统调用中断返回时调用了本函数. 如果是系统调用, 则参数 eax 是系统调用的返回值.
	// 在 kernel/exit.c 的 waitpid() 函数中, 如果收到了 SIGCHLD 信号, 或者在读管道函数 fs/pipe.c 中, 
	// 管道当前读数据但没有读到任何数据等情况下, 进程收到了任何一个非阻塞的信号, 则都会返回 -ERESTARTSYS. 
	// 它表示进程可以被中断, 但是在继续执行后会重新启动系统调用. 
	// 返回码 -ERESTARTNOINTR 说明在处理完信号后一定会重启系统调用(不会受信号影响), 即系统调用不会被中断(NOINTR). 
	// 如果当前是系统调用返回, 并且系统调用函数的返回值 eax 表明当前需要重新执行系统调用.
	if ((orig_eax != -1) && ((eax == -ERESTARTSYS) || (eax == -ERESTARTNOINTR))) { 	// 判断要不要重启系统调用, 并做相应处理.
		// 如果系统调用被信号中断(一般是**系统调用里主动判断有没有信号到来**, 如果来了特定的信号则系统调用返回 ERESTARTSYS)且想要重启系统调用, 
		// 但是信号不允许重启(SA_INTERRUPT)或者信号值是 xxx, 则不能重启系统调用.
		// 信号值小于 SIGCONT 或者大于 SIGTTOU(即信号不是 SIGCONT, SIGSTOP, SIGTSTP, SIGTTIN 或 SIGTTOU), 
		// 则修改系统调用的返回值为 eax = -EINTR, 即系统调用被信号中断(由系统调用主动判断是否有某种信号到来, 比如 sys_waitpid()).
		if ((eax == -ERESTARTSYS) && ((sa->sa_flags & SA_INTERRUPT) || signr < SIGCONT || signr > SIGTTOU)) { 		
			*(&eax) = -EINTR;
		// 否则(系统调用想重启, 并且当前信号允许重启, 当前信号也不是 xxx）重启系统调用: 恢复进程寄存器 eax 在调用系统调用之前的值, 
		// 并且把用户态程序指令指针回调 2 个字节. 即当返回用户态时, 让程序重新启动执行被信号中断的系统调用. 
		} else { 			// 如果返回值是 -ERESTARTNOINTR 则表示必须重启系统调用(只有 sys_sigsuspend使用了这个信号), 忽略信号的影响.
			*(&eax) = orig_eax;     				// orig_eax: 用户态下指定的系统调用号.
			// 系统调用返回到用户态的时候再次执行本次系统调用.
			old_eip -= 2; 							// 'int 0x80' 占两个字节.
			__asm__ ("movl %%eax, %0\n\t" : : "m" (eip), "a" (old_eip));
		}
	}
	// 如果信号处理句柄为 SIG_IGN(1, 默认忽略句柄)则不对信号进行处理而直接返回. 
	sa_handler = (unsigned long) sa->sa_handler;
	if (sa_handler == 1) {
		return(1);   								/* Ignore, see if there are more signals... */
	}
	// 如果句柄为 SIG_DFL(0, 默认处理), 则根据具体的信号进行分别处理. 
	if (!sa_handler) {
		switch (signr) {
			// 如果信号是以下两个则也忽略之, 并返回.
			case SIGCONT: 								// 恢复进程, 继续执行.
			case SIGCHLD:								// 子进程停止或被中止.
				return(1);  							/* Ignore, ... */

			// 如果信号是以下 4 种信号之一, 则把当前进程状态置为停止状态 TASK_STOPPED, 并设置进程退出值为信号值. 
			// 若当前进程父进程对 SIGCHLD 信号的 sigaction 没有设置处理标志 SA_NOCLDSTOP, 
			// 即没有要求当子进程停止执行或又继续执行时不要产生 SIGCHLD 信号, 则给父进程发送 SIGCHLD 信号. 
			case SIGSTOP: 								// 停止进程的执行.
			case SIGTSTP: 								// TTY 发出停止进程, 可忽略.
			case SIGTTIN: 								// 后台进程请求输入.
			case SIGTTOU: 								// 后台进程请求输出.
				current->state = TASK_STOPPED;
				current->exit_code = signr;
				if (!(current->p_pptr->sigaction[SIGCHLD - 1].sa_flags & SA_NOCLDSTOP)) {
					current->p_pptr->signal |= (1 << (SIGCHLD - 1));
				}
				return(1);  							/* Reschedule another event */

			// 如果信号是以下 6 种信号之一, 那么若信号产生了 core_dump, 则以退出码为 signr|0x80 调用 do_exit() 退出. 
			// 否则退出码就是信号值. do_exit()的参数是返回码和程序提供的退出状态信息. 
			// 可作为 wait() 或 waitpid() 函数的状态信息. 参见sys/wait.h; 
			// wait() 或 waidpid() 利用这些宏就可以取得子进程的退出状态码或子进程终止的原因(信号). 
			case SIGQUIT:
			case SIGILL:
			case SIGTRAP:
			case SIGIOT:
			case SIGFPE:
			case SIGSEGV:
				if (core_dump(signr)) {
					do_exit(signr | 0x80);   			// 或上 0x80 用于标志是否转储.
				}
				/* fall through */
			default:
				do_exit(signr);
		}
	}
	/*
	 * OK, we're invoking a handler
	 */
	// 如果信号处理句柄不是 0, 也不是 1, 则调用信号处理函数.
	// 如果信号行为控制属性中设置了一次性调用标志, 则清除对应的处理函数, 下次使用上面的 0 号句柄(默认)处理.
	if (sa->sa_flags & SA_ONESHOT) {
		sa->sa_handler = NULL; 						// 清空处理函数.
	}
	// 此时用户程序返回地址(即中断返回地址 eip, cs)被保存在内核态栈中. 
	// 下面这段代码修改内核态堆栈上用户调用时的代码指针 eip 为指向信号处理句柄, 
	*(&eip) = sa_handler; 							// 修改系统调用或中断返回到用户程序时, 执行信号处理函数.
	// 如果不屏蔽相同信号(SA_NOMASK), 则也需要将进程的信号屏蔽码压入堆栈(为了防止信号处理程序的递归调用, 一般是会屏蔽信号的). 
	// 同时也将 sa_restorer, signr, 信号屏蔽码(如果 SA_NOMASK 没置位), eax, ecx, edx 
	// 作为参数以及原调用系统调用的程序返回指针及标志寄存器值压入用户堆栈. 
	// 因此在本次系统调用返回用户程序时会首先执行信号处理程序, 然后继续执行用户程序. 
	longs = (sa->sa_flags & SA_NOMASK) ? 7 : 8; 	// 不需要屏蔽相同信号时为 7, 需要屏蔽时为 8(多了个屏蔽码).
	// 将原调用程序的用户堆栈指针向下扩展 7(或 8)个长字(用来存放调用信号句柄的参数等), 
	// 并检查内存使用情况(如内存超界则分配新页等). 
	__asm__("subl %1, %0\n\t" : : "m" (esp), "a" (longs * 4)); 		// 用户栈指针向栈顶方向移动 7/8 个 4byte 空间.
	verify_area(esp, longs * 4);
	// 在用户堆栈中从下到上存放 sa_restorer, 信号 signr, 屏蔽码 blocked(如果 SA_NOMASK 置位), 
	// eax, ecx, edx, eflags 和原来用户中断返回时的代码指针(实现信号处理程序执行完后执行原用户程序). 
	tmp_esp = esp;
	put_fs_long((long) sa->sa_restorer, tmp_esp++);
	put_fs_long(signr, tmp_esp++);
	if (!(sa->sa_flags & SA_NOMASK)) {				// 如果没有设置不需要屏蔽(也就是说要屏蔽), 则压入屏蔽码.
		put_fs_long(current->blocked, tmp_esp++); 	// 将当前进程的信号屏蔽码压入栈中.
	}
	put_fs_long(eax, tmp_esp++);
	put_fs_long(ecx, tmp_esp++);
	put_fs_long(edx, tmp_esp++);
	put_fs_long(eflags, tmp_esp++);
	put_fs_long(old_eip, tmp_esp++); 				// 压入原来用户中断返回代码地址, 实现信号处理程序执行完后再执行原用户程序.
	current->blocked |= sa->sa_mask;                // 进程信号屏蔽码中加上 sa_mask 中的码. 
	// 由于该函数在 ret_from_sys_call 中被进程的每个信号都调用一次.
	return(0);										/* Continue, execute handler */
}
