/*
 *  linux/kernel/sys_call.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  system_call.s  contains the system-call low-level handling routines.
 * This also contains the timer-interrupt handler, as some of the code is
 * the same. The hd- and flopppy-interrupts are also here.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call. Ordinary interrupts
 * don't handle signal-recognition, as that would clutter them up totally
 * unnecessarily.
 *
 * Stack layout in 'ret_from_system_call':
 *
 *	 0(%esp) - %eax
 *	 4(%esp) - %ebx
 *	 8(%esp) - %ecx
 *	 C(%esp) - %edx
 *	10(%esp) - original %eax	(-1 if not system call)
 *	14(%esp) - %fs
 *	18(%esp) - %es
 *	1C(%esp) - %ds
 *	20(%esp) - %eip
 *	24(%esp) - %cs
 *	28(%esp) - %eflags
 *	2C(%esp) - %oldesp
 *	30(%esp) - %oldss
 */
/*
 * sys_call.s 文件包含系统调用(system-call)底层处理子程序. 
 * 由于有些代码比较类似, 所以同时也包括时钟处理(timer-interrupt)句柄.
 * 硬盘和软盘的中断处理程序也在这里.
 *
 * 注意: 这段代码处理信号(signal)识别, 在每次时钟中断和系统调用之后都会进行识别. 
 * 一般中断过程并不处理信号识别, 因为会给系统造成混乱.
 *
 * 从系统调用返回(ret_from_system_call)时堆栈的内容见上面.
 */
# 上面 Linus 原注释中一般中断过程是指除了系统调用中断(int 0x80)和时钟中断(int 0x20)以外的其他中断. 
# 这些中断会在内核态或用户态随机发生, 若在这些中断过程中也处理信号识别的话, 
# 就有可能与系统调用中断和时钟中断过程中对信号的识别处理过程相冲突, 违反了内核代码非抢占原则.
# 因此系统既无必要在这些 "其他" 中断中处理信号, 也不允许这样做.

SIG_CHLD	= 17					# 定义 SIG_CHLD 信号(子进程停止或结束).

EAX			= 0x00					# 堆栈中各个寄存器的偏移位置.
EBX			= 0x04
ECX			= 0x08
EDX			= 0x0C
ORIG_EAX	= 0x10					# 如果不是系统调用(是其他中断)时, 该值为 -1.
FS			= 0x14
ES			= 0x18
DS			= 0x1C
EIP			= 0x20					# 以下行由 CPU 自动入栈.
CS			= 0x24
EFLAGS		= 0x28
OLDESP		= 0x2C					# 当特权级变化时, 原堆栈指针也会入栈.
OLDSS		= 0x30

# 以下这些是任务结构(task_struct)中变量的偏移值, 参见 include/linux/sched.h
state		= 0						# these are offsets into the task-struct.	# 进程状态码.
counter		= 4						# 任务运行时间计数(递减)(滴答数), 运行时间片.
priority 	= 8						# 运行优先数. 任务开始运行时 counter = priority, 越大则运行时间越长.
signal		= 12					# 是信号位图, 每个位代表一种信号, 信号值 = 位偏移值 + 1.
sigaction 	= 16					# MUST be 16 (=len of sigaction)	# sigaction 结构长度必须是 16 字节.
blocked 	= (33*16)				# 受阻塞信号位图的偏移量.

# 以下定义在 sigaction 结构中的偏移量, 参见 include/signal.h
# offsets within sigaction
sa_handler 	= 0						# 信号处理过程的句柄(描述符)
sa_mask 	= 4						# 信号屏蔽码
sa_flags 	= 8						# 信号集.
sa_restorer = 12					# 恢复函数指针, 参见 kernel/signal.c 程序说明.

nr_system_calls = 82				# Linux 0.12 版内核中的系统调用总数.

ENOSYS = 38							# 系统调用号出错码.

/*
 * Ok, I get parallel printer interrupts while using the floppy for some
 * strange reason. Urgel. Now I just ignore them.
 */
/*
 * 好了, 在使用软驱时我收到了并行打印机中断, 很奇怪. 呵, 现在不管它.
 */
.globl system_call, sys_fork, timer_interrupt, sys_execve
.globl hd_interrupt, floppy_interrupt, parallel_interrupt
.globl device_not_available, coprocessor_error, sys_default

# 系统调用号错误时返回出错码 - ENOSYS
.align 4							# 内存 4 字节对齐.
bad_sys_call:
	pushl $-ENOSYS					# eax 中置 - ENOSYS.
	jmp ret_from_sys_call 			# jmp 和 call 的区别是 jmp 直接跳转到地址处开始执行, 
									# 而 call 会先将返回地址入栈然后调到标号处开始执行

# 重新执行调度程序入口. 调度程序 schedule() 在(kernel/sched.c)
# 当调度程序 schedule() 返回时就从 ret_from_sys_call 处继续执行.
.align 4
reschedule:
	pushl $ret_from_sys_call		# 将 ret_from_sys_call 的地址入栈.
	jmp schedule					# jmp 和 call 的区别是 jmp 直接跳转到地址处开始执行, 
									# 而 call 会先将返回地址入栈然后调到标号处开始执行

# CPL(Current Privilege Level): 当前特权级, 当前程序或任务的特权级, 存放在 cs 和 ss 寄存器的位 0/1 上.
# DPL(Descriptor Privilege Level): 描述符特权级, 一个段或门的特权级, 存放在段或门描述符的 DPL 字段中.
# RPL(Request Privilege Level): 请求特权级, 存放在选择符的位 0/1 上.
# RPL 和 CPL 会与 DPL 进行对比, 如果大于(特权级低)则不允许调用.

# 在 TASK-0 调用该中断创建 TASK-1 时, TASK-0 的 CPL = 3, 用户态代码通过陷阱门(调用门)来调用该中断代码.
# 该中断的 DPL = 0, 需要进行特权级切换, 中断和任务使用不同的堆栈(中断处理程序使用任务 ss0, sp0 字段指定的堆栈).

# int 0x80 -- Linux 系统调用入口点(调用中断 int 0x80, eax 中是调用号).
# 中断调用服务列表在 sys_call_table 中(include/linux/sys.h).
# system_call 陷阱门在 GDT 中的特权级(DPL)是 3, 即所有特权级的代码都可以调用.
.align 4
# 因为用户态代码在调用 int 0x80 时会发生堆栈切换, 所以此时的堆栈为当前任务 TSS 指定的内核态堆栈(ss0 = 0x10)
system_call:
	push %ds						# 保存原(调用方)段寄存器值.
	push %es
	push %fs
	pushl %eax						# save the orig_eax		# 保存 eax 原值(系统调用号).
	# 一个系统调用最多可带有 3 个参数, 也可以不带参数. 
	# 下面入栈的 ebx, ecx 和 edx 中放着系统调用相应 C 语言函数的调用参数.
	# 这几个寄存器入栈的顺序是由 GNU gcc 规定的, ebx 中可存放第 1 个参数, 
	# ecx 中存放第 2 个参数, edx 中存放第 3 个参数.
	# 系统调用语句可参见头文件 include/unistd.h 中的系统调用宏.
	pushl %edx
	pushl %ecx						# push %ebx, %ecx, %edx as parameters
	pushl %ebx						# to the system call
	# 在保存过段寄存器之后, 让 ds, es 指向内核数据段, 而 fs 指向当前任务的局部数据段, 
	# 即指向执行本次系统调用的用户程序的数据段. (内核数据段基地址是 0x0, 所以内核态访问 0-16MB 的内存空间时, 线性地址与物理地址相同)
	# 注意, 在 Linux0.12 中内核给任务分配的代码和数据内存段是重叠的, 它们的段基址和段限长相同.
	movl $0x10, %edx				# set up ds, es to kernel space.  	// 当前的 CPL = 0, 选择符 0x10 的 RPL = 0.
	mov %dx, %ds 					# ds/es 指向内核数据段.
	mov %dx, %es
	movl $0x17, %edx				# fs points to local data space.
	mov %dx, %fs 					# fs 指向任务局部数据段.
	cmpl NR_syscalls, %eax 			# 如果 eax 中的值大于 NR_syscalls 则表示超出调用号范围(>= 87).
	jae bad_sys_call 				# 调用号如果超出范围(>= 87)的话就跳转.

	# 把 sys_call_table[4 * %eax] 确定的内存地址处的内容(系统调用程序的地址)放入 ebx 中
    mov sys_call_table(, %eax, 4), %ebx		# (include/linux/sys.h)
    cmpl $0, %ebx 							# 判断该系统调用入口地址是否为 0. 
    jne sys_call 							# 不为 0 则执行系统调用.
	#	pushl %eax
    call sys_default
	# 下面这句操作数的含义是: 调用地址 = (sys_call_table + %eax * 4).
	# sys_call_table[] 是一个指针数组, 定义在 include/linux/sys.h 中, 
	# 该数组中设置了内核所有 82 个系统调用 C 处理函数的地址.
sys_call:
	# call 指令会将下一行代码的地址(`pushl %eax`)压入栈中.
	call *%ebx 							# ebx 中含有被调用函数的地址, * 表示间接调用. 
										# 调用 eax 调用号指定的系统调用函数. (此处不会发生特权级切换)
	# call *sys_call_table(, %eax, 4)	# 间接调用指定功能 C 函数.
	pushl %eax							# 把系统调用返回值入栈.

# 如果当前进程不处于可运行状态或时间片已用完, 则执行进程调度.
# 例如当后台进程组中的进程执行控制终端读写操作时, 那么默认条件下该后台进程组有进程会收到 SIGTTIN 或 SIGTTOU 信号,
# 导致进程组中所有进程处于停止状态. 而当前进程则会立刻返回.
2:
	movl current, %eax				# 取当前任务(进程)数据结构指针 -> eax.
	cmpl $0, state(%eax)			# state(%eax): task_struct->state, 如果当前进程状态不处于 0(TASK_RUNNING)则执行进程调度.
	jne reschedule
	cmpl $0, counter(%eax)			# counter, 如果当前进程剩余的执行时间为 0 调度其它进程运行.
	je reschedule

# 以下这段代码执行从系统调用功能函数或者时钟中断函数返回后, 对信号进行识别处理. 
# 其它中断服务程序退出时也将跳转到这里进行处理后才退出中断过程, 例如后面的处理器出错中断 int 16.
# 首先判断当前任务是不是初始任务 TASK-0, 如果是则不必对其进行信号方面的处理, 直接返回.
# task 对应 C 程序中的 task[] 数组, 直接引用 task 相当于引用 task[0].
ret_from_sys_call:
	movl current, %eax 				# 获取当前进程, current 定义在 (kernel/sched.c)
	# 判断当前进程是否是系统的 0 号进程, 如果是， 则不用进行信号处理, 直接返回.
	cmpl task, %eax					# task[0] cannot have signals
	je 3f							# 向前(forward)跳转到标号 3 处退出中断处理.
	# 通过对当前进程的代码段选择符来判断进程被中断(系统调用也是中断)前是否处于用户态. 
	# **如果是内核态(即在内核态下被中断或调用了系统调用)则直接退出中断**.
	# 这是因为任务在内核态执行时不可抢占(即不能执行后面的 reschedule 来打断当前进程, 不过进程可以睡眠并主动让出 cpu, 
	# 比如读取硬盘数据时就处于内核态, 但是进程会主动调用 schedule() 来进入睡眠并让出 cpu). 
	# 另外, 如果原堆栈段选择符不为 0x17(即原堆栈不在用户段中), 也说明不是用户态代码, 则也退出.
	cmpw $0x0f, CS(%esp)			# was old code segment supervisor?
	jne 3f 							# 如果不是用户态代码, 则直接准备返回. 
	cmpw $0x17, OLDSS(%esp)			# was stack segment = 0x17?
	jne 3f 							# 如果原堆栈不是用户态堆栈, 则直接准备返回.

	# 如果当前进程中断前是处于用户态, 则处理当前进程中的信号. 
	# 首先取当前任务结构中的信号位图(32 位, 每位代表 1 种信号), 然后用任务结构中的信号屏蔽码来屏蔽不需要处理的信号, 
	# 从 0 号信号开始依次取各个置位的信号, 并把信号位图中该信号对应的位复位(置 0), 最后将该信号作为参数调用 do_signal().
	# do_signal()在(kernel/signal.c)中, 其参数包括 13 个入栈信息. 在 do_signal() 或信号处理函数返回之后, 
	# 若返回值不为 0 则再看看是否需要切换进程或继续处理其他信号.
	movl signal(%eax), %ebx			# 取信号位图 -> ebx, 每 1 位代表 1 种信号, 共 32 个信号.
	movl blocked(%eax), %ecx		# 取屏蔽信号位图 -> ecx. 屏蔽信号位图用于指明哪些信号不需要处理.
	notl %ecx						# 每位取反.
	andl %ebx, %ecx					# 获取许可的(屏蔽后剩余的需要处理的)信号位图.
	bsfl %ecx, %ecx					# 从低位(位 0)开始扫描位图, 看是否有置 1 的位, 若有, 则 ecx 保留该位的偏移值(即地址位 0--31).
	je 3f							# 如果没有信号要处理则向前跳转退出.
	btrl %ecx, %ebx					# 如果有要处理的信号, 则复位该信号(ebx 含有原 signal 位图).
	movl %ebx, signal(%eax)			# 重新将 signal 位图信息保存到 current -> signal.
	incl %ecx						# 将信号调整为从 1 开始的数(1--32).
	pushl %ecx						# 信号值入栈作为调用 do_signal 的参数之一.
	call do_signal					# 调用 C 函数信号处理程序(kernel/signal.c)
	popl %ecx						# 弹出入栈的信号值.
	testl %eax, %eax				# 测试返回值, 若不为 0 则跳转到前面标号 2 处.
	jne 2b							# see if we need to switch tasks, or do more signals.
3:	popl %eax						# eax 中含有 sys_call 中入栈(`pushl %eax`)的系统调用返回值.
	popl %ebx 						# 这三个寄存器是进行系统调用时的参数. (第一个参数)
	popl %ecx						# 第二个.
	popl %edx 						# 第三个.
	addl $4, %esp					# skip orig_eax. 丢弃原 eax 值(原来是保存的系统调用号).
	pop %fs							# 恢复系统调用/中断前各个段寄存器选择符.
	pop %es
	pop %ds
	iret 							# 系统调用结束(如果是用户态调用, 还会主动进行堆栈切换).

# int16 -- 处理器错误中断. 类型: 错误; 无错误码.
# 这是一个外部的基于硬件的异常. 当协处理器检测到自己发生错误时, 就会通过 ERROR 引脚通知 CPU. 
# 下面代码用于处理协处理器发出的出错信号. 并跳转去执行 C 函数 math_error()(kernel/math/error.c).
# 返回后将跳转到标号 ret_from_sys_call 处继续执行.
.align 4
coprocessor_error:
	push %ds
	push %es
	push %fs
	pushl $-1						# fill in -1 for orig_eax	# 填 -1, 表明不是系统调用.
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10, %eax				# ds, es 置为指向内核数据段.
	mov %ax, %ds
	mov %ax, %es
	movl $0x17, %eax				# fs 置为指向局部数据段(出错程序的数据段).
	mov %ax, %fs
	pushl $ret_from_sys_call		# 把下面调用返回的地址入栈.
	jmp math_error					# 执行 math_error()(kernel/math/error.c).

# int7 -- 设备不存在或协处理器不存在. 类型: 错误; 无错误码.
# 如果控制寄存器 CR0 中 EM(模拟)标志置位, 则当 CPU 执行一个协处理器指令时就会引发该中断, 
# 这样 CPU 就可以有机会让这个中断处理程序模拟处理器指令. 
# CR0 的交换标志 TS 是在 CPU 执行任务转换时设置的. 
# TS 可以用来确定什么时候协处理器中的内容与 CPU 正在执行的任务不匹配了. 
# 当 CPU 在运行一个协处理器转移指令时发现 TS 置位时, 就会引发该中断.
# 此时就可以保存前一个任务的协处理器内容, 并恢复新任务的协处理器执行状态. 参见 (kernel/sched.c)
# 该中断最后将转移到标号 ret_from_sys_call 处执行下去(检测并处理信号).
.align 4
device_not_available:
	push %ds
	push %es
	push %fs
	pushl $-1						# fill in -1 for orig_eax	# 填 -1, 表明不是系统调用.
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10, %eax				# ds, es 置为指向内核数据段.
	mov %ax, %ds
	mov %ax, %es
	movl $0x17, %eax				# fs 置为指向局部数据段(出错程序的数据段).
	mov %ax, %fs
	# 清 CR0 中任务已交换标志 TS, 并取 CR0 值. 若其中协处理器仿真标志 EM 没有置位, 
	# 说明不是 EM 引起的中断, 则恢复任务协处理器状态, 
	# 执行 C 函数 math_state_restore(), 并在返回时去执行 ret_from_sys_call 处的代码.
	pushl $ret_from_sys_call		# 把下面跳转或调用的返回地址入栈.
	clts							# clear TS so that we can use math
	movl %cr0, %eax
	testl $0x4, %eax				# EM (math emulation bit)
	je math_state_restore			# 执行 math_state_restore()(kernel/sched.c)
	# 若 EM 标志置位, 则只执行数学仿真程序 math_emulate().
	pushl %ebp
	pushl %esi
	pushl %edi
	pushl $0						# temporary storage for ORIG_EIP
	call math_emulate				# 调用 C 函数(math/math_emulate.c)
	addl $4, %esp					# 丢弃临时存储.
	popl %edi
	popl %esi
	popl %ebp
	ret								# 这里的 ret 将跳转到 ret_from_sys_call.

# int32 -- (int 0x20)时钟中断处理程序. 中断频率设置为 100Hz(include/linux/sched.h), 
# 定时芯片 8253/8254 是在(kernel/sched.c)处初始化的. 因此这里 jiffies 每 10 毫秒加 1. 
# 这段代码将 jiffies 增加 1, 发送结束中断指令给 8259 控制器, 
# 然后用当前特权级作为参数调用 C 函数 do_timer(long CPL). 当调用返回时转去检测并处理信号.
# 触发中断时, 如果特权级(3 -> 0)发生变化会导致堆栈切换, 没变化时使用原堆栈, 所以压栈信息有区别: 
# 堆栈切换时, 入栈的数据有 eflags/cs/eip/出错码(时钟中断不会有出错码), 不切换时, 入栈 ss/esp/eflags/cs/eip/出错码(没有).
# 所以无论如何, 此时栈顶的数据都是: eflags/cs/eip.
.align 4
timer_interrupt:
	# 在此之前中断触发时 CPU 已经入栈了进程的 eflags/cs/eip.
	# 下面的入栈操作是模拟与系统调用 system_call 时一样的入栈数据, 从而在中断返回(ret_from_sys_call)时调用其它函数(比如 do_signal())时可以有相同的参数格式.
	# 但是为了与系统调用区分, 将 orig_eax 设置为 -1, 表明不是由系统调用执行中断返回.
	push %ds						# save ds, es and put kernel data space
	push %es						# into them. %fs is used by _system_call
	push %fs						# 保存 ds, es 并让其指向内核数据段. fs 将用于 system_call.
	pushl $-1						# fill in -1 for orig_eax	# 填 -1, 表明不是系统调用(系统调用时保存的是功能号).
	# 下面我们保存寄存器 eax, ecx 和 edx. 这是因为 gcc 编译器在调用函数时不会保存它们. 
	# 这里也保存了 ebx 寄存器, 因为在后面 ret_from_sys_call 中会用到它.
	pushl %edx						# we save %eax, %ecx, %edx as gcc doesn't
	pushl %ecx						# save those across function calls. 
	pushl %ebx						# %ebx is saved as we use that in ret_sys_call
	pushl %eax
	movl $0x10, %eax				# ds, es 指向内核数据段.
	mov %ax, %ds
	mov %ax, %es
	movl $0x17, %eax				# fs 指向局部数据(程序的数据段).
	mov %ax, %fs
	incl jiffies 					# 系统滴答数 jiffies +1.
	# 由于初始化中断控制芯片时没有采用自动 EOI, 所以这里需要发指令结束该硬件中断(表示该中断已得到响应).
	movb $0x20, %al					# EOI to interrupt controller #1
	outb %al, $0x20
	# CS(%esp) 表示取 %esp + CS(0x24) 内存地址处的数据(栈中保存的被中断进程的 CS 寄存器).
	# 然后 & $3 后得到被中断进程的 CPL, 作为参数传给 do_timer(long cpl) 的参数.
	movl CS(%esp), %eax 			
	andl $3, %eax					# %eax is CPL (0 or 3, 0 = supervisor) 只保留低两位, 即只保留进程的 CPL.
	pushl %eax
	# 调用 do_timer() 函数执行任务切换, 计时等工作, 在 kernel/sched.c 实现.
	call do_timer					# 'do_timer(long CPL)' does everything from
	addl $4, %esp					# task switching to accounting ...
	jmp ret_from_sys_call

# 这是 sys_execve() 系统调用. 实际调用 c 函数 do_execve(). (fs/exec.c)
.align 4
sys_execve:
	# eax 中保存的是栈中指向用户调用 `int 0x80` 代码下一行地址的栈指针地址. 即 eax 中是指向栈中的一个地址.
	lea EIP(%esp), %eax				# (%esp + 0x20) 当前栈顶向下偏移 0x20 处保存的是: (下一行注释)
	pushl %eax 						# 用户态代码调用 0x80 中断的下一行代码地址, 即中断返回后要执行的代码.
	call do_execve 					# 调用 c 函数 do_execve().
	addl $4, %esp					# 丢弃上面调用 do_execve 前压入栈的 EIP 值.
	ret

# sys_fork() 调用, 用于创建子进程, 是 system_call 功能 2. 原型在 include/linux/sys.h 中.
# 首先调用 C 函数 find_empty_process(), 取得一个进程号 last_pid 及任务项号 task[i]. 
# 若返回负数则说明目前任务数组已满. 然后调用 copy_process() 复制进程.
.align 4
sys_fork:
	call find_empty_process			# 为新进程取得进程号 last_pid(kernel/fork.c)
	testl %eax, %eax				# 在 eax 中返回任务项号(非 pid). 若返回负数(task[] 中没有空闲任务项)则退出.
	js 1f 							# 如果为负数, 则直接返回.
	push %gs 						# 第五个参数
	pushl %esi						# 第四个参数
	pushl %edi						# 第三个参数
	pushl %ebp 						# 第二个参数
	pushl %eax 						# eax 中是调用 copy_process 时的第一个参数 nr.
	call copy_process				# 调用 C 函数 copy_process()(kernel/fork.c)
	addl $20, %esp					# 丢弃这里所有压栈内容(eax/ebp/edi/esi/gs).
1:	ret 							# 返回值是新进程的 pid(last_pid), 存放在 eax 中.

# int 46 -- (int 0x2E) 硬盘中断处理程序, 响应硬盘中断请求 IRQ14.
# 当请求的硬盘操作完成或出错就会发出此中断信号. (参见 kernel/blk_drv/hd.c).
# 首先向 8259A 中断控制从芯片发送结束硬件中断指令(EOI), 然后取变量 do_hd 中的函数指针放入 edx 寄存器中, 
# 并置 do_hd(在 kernel/blk_drv/blk.h 中定义 void (*DEVICE_INTR)(void) = NULL;) 为 NULL, 接着判断 edx 函数指针是否为空. 
# 如果为空, 则给 edx 赋值指向 unexpected_hd_interrupt(), 用于显示出错信息. 
# 随后向 8259A 主芯片送 EOI 指令, 并调用 edx 中指针指向的函数: 
# read_intr(), write_intr() 或 unexpected_hd_interrupt().
hd_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10, %eax				# 使 ds, es 指向内核数据段.
	mov %ax, %ds
	mov %ax, %es
	movl $0x17, %eax				# fs 当前进程的局部数据段.
	mov %ax, %fs
	# 由于初始化中断控制芯片时没有采用自己 EOI, 所以这里要发指令结束该硬件中断.
	movb $0x20, %al
	outb %al, $0xA0					# EOI to interrupt controller #1	# 送从 8259A
	jmp 1f							# give port chance to breathe		# 这里 jmp 起延时作用.
1:	jmp 1f
	# do_hd 定义为一个函数指针, 将被赋值为函数 read_intr() 或 write_intr() 的地址. 
	# 放到 edx 寄存器后就将 do_hd 指针变量置为 NULL. 
	# 然后测试得到的函数指针, 若该指针为空, 则赋予该指针指向 C 函数 unexpected_hd_interrupt(), 以处理未知硬盘中断.
1:	xorl %edx, %edx
	movl %edx, hd_timeout			# hd_timeout 置为 0. 表示控制器已在规定时间内产生了中断.
	xchgl do_hd, %edx 				# schgl 汇编指令: 将一个字节或一个字的源操作数和目的操作数相交换
	testl %edx, %edx
	jne 1f							# 若空, 则让指针指向 C 函数 unexpected_hd_interrupt().
	movl $unexpected_hd_interrupt, %edx
1:	outb %al, $0x20					# 送 8259A 主芯片 EOI 指令(结束硬件中断).
	call *%edx						# "interesting" way of handling intr. 	# 调用 do_hd 指向的 C 函数.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

# int38 -- (int 0x26)软盘驱动器中断处理程序, 响应硬件中断请求 IRQ6.
# 其处理过程与上面对硬盘的处理基本一样. (kernel/blk_drv/floppy.c).
# 首先向 8259A 中断控制器主芯片发送 EOI 指令, 然后取变量 do_floppy 中的函数指针放入 eax 寄存器中, 
# 并置 do_floppy 为NULL, 接着判断 eax 函数指针是否为空. 如为空, 
# 则给 eax 赋值张贴 unexpected_floppy_interrupt(), 用于显示出错信息. 
# 随后调用 eax 指向的函数: 
# rw_interrupt, seek_interrupt, recal_interrupt, reset_interrupt 或 unexpected_floppy_interrupt.
floppy_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10, %eax				# ds, es 置为内核数据段.
	mov %ax, %ds
	mov %ax, %es
	movl $0x17, %eax				# fs 置为调用程序的局部数据段.
	mov %ax, %fs
	movb $0x20, %al					# 送主 8259A 中断控制器 EOI 指令(结束硬件中断).
	outb %al, $0x20					# EOI to interrupt controller #1
	xorl %eax, %eax
	# do_floppy 为函数指针, 将被赋值实际处理 C 函数指针. 该指针在被交换放到 eax 寄存器后就将 do_floppy 变量置空. 
	# 然后测试 eax 中原指针是否为空, 若是则使指针指向 C 函数 unexpected_floppy_interrupt().
	xchgl do_floppy, %eax
	testl %eax, %eax				# 测试函数指针是否 = NULL?
	jne 1f							# 若空, 则使指针 C 函数 unexpected_floppy_interrupt().
	movl $unexpected_floppy_interrupt, %eax
1:	call *%eax						# "interesting" way of handling intr.	# 间接调用.
	pop %fs							# 上句调用 do_floppy 指向的函数.
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

# int 39 -- (int 0x27)并行口中断处理程序, 对应硬件中断请求信号 IRQ7.
# 本版本内核还未实现. 这里只是发送 EOI 指令.
parallel_interrupt:
	pushl %eax
	movb $0x20, %al
	outb %al, $0x20
	popl %eax
	iret
