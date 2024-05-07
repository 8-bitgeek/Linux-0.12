/*
 *  linux/boot/head.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  head.s contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00000000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory.
 */
/*
 * head.s 含有 32 位启动代码. 
 * 注意!!! 32 位启动代码是从绝对地址 0x00000000 开始的, 这里也同样是页目录将存在的地方, 因此这里的启动代码将被页目录覆盖掉.
 */
.text
.globl idt, gdt, pg_dir, tmp_floppy_area
pg_dir: 		# 页目录将会存放在这里.
# 再次注意!!! 这里已经处于 32 位运行模式, 因此这里的 $0x10 并不是把地址 0x10 装入各个段寄存器, 它现在其实是全局段描述符表中的偏移值, 
# 或者更准确地说是一个全局描述符表项的选择符. 这里 $0x10 的含义是请求特权级 0(位 0-1 = 0), 
# 选择全局描述符表(位 2 = 0), 选择表中第 2 项(位 3-15 = 2), 指向表中的数据段描述符项.
# 下面的代码含义是: 设置 ds, es, fs, gs 为 setup.s 中构造的数据段(全局段描述符表第 2 项)的选择符 = 0x10, 
# 并将堆栈放置在 stack_start 指向的 user_stack 数据区, 然后使用本程序后面定义的新中断描述符表和全局段描述符表. 
# 新全局段描述表中初始内容与 setup.s 中的基本一样, 仅段限长从 8MB 修改成了 16MB.
# stack_start 定义在 kernel/sched.c 中. 它是指向 user_stack 数组末端的一个长指针. 
# 下面设置这里使用的栈, 姑且称为系统栈. 但在移动到任务 0 执行(init/main.c 中)以后该栈就被用作任务 0 和任务 1 共同使用的用户栈了.
.globl startup_32
startup_32:
	movl $0x10, %eax	# 对于 GNU 汇编, 每个直接操作数要以 '$' 开始, 否则表示地址. 每个寄存器名都要以 '%' 开头, eax 表示是 32 位的 ax 寄存器.
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	# stack_start 定义在 kernel/sched.c 中. ss = 0x10.
	lss stack_start, %esp				# 表示将 stack_start 地址中的内容加载到 ss:esp, 设置系统堆栈. 
	call setup_idt						# 调用设置中断描述符表子程序.
	call setup_gdt						# 调用设置全局描述符表子程序.
	movl $0x10, %eax					# reload all the segment registers
	mov %ax, %ds						# after changing gdtgdbgdb 调试 32 位 调试 32 位. CS was already
	mov %ax, %es						# reloaded in 'setup_gdt'
	mov %ax, %fs						# 因为修改了 gdt, 所以需要重新装载所有的段寄存器. 
	mov %ax, %gs 						# CS 代码段寄存器已经在 setup_gdt 中重新加载过了.
	# 由于段描述符中的段限长从 setup.s 中的 8MB 改成了本程序设置的 16MB, 因此这里再次对所有段寄存器执行加载操作是必须的. 
	# 另外, 通过使用 bochs 跟踪观察, 如果不对 CS 再次执行加载, 那么在执行到 movl $0x10, %eax 时 CS 代码段不可见部分中的限长还是 8MB.
	# 这样看来应该重新加载 CS. 但是由于 setup.s 中的内核代码段描述符与本程序中重新设置的代码段描述符除了段限长以外其余部分完全一样, 
	# 8MB 的限长在内核初始化阶段不会有问题, 而且在以后内核执行过程中段间跳转时会重新加载 CS. 因此这里没有加载它并没有让程序出错. 
	# 针对该问题, 目前内核中就在 call setup_gdt 之后添加了一条长跳转指令: 'ljmp $(__KERNEL_CS), $1f', 
	# 跳转到 movl $0x10, $eax 来确保 CS 确实被重新加载.
	lss stack_start, %esp

	# 下面代码用于测试 A20 地址线是否已经开启. 采用的方法是向内存地址 0x000000 处写入任意一个数值, 
	# 然后看内存地址 0x100000(1M) 处是否也是这个数值. 如果一直相同的话, 就一直比较下去, 即死循环, 死机. 
	# 表示地址 A20 线没有选通, 结果内核就不能使用 1MB 以上内存.
	xorl %eax, %eax
1:	incl %eax							# check that A20 really IS enabled
	# '1:' 是一个局部符号构成的标号. 标号由符号后跟一个冒号组成. 此时该符号表示活动位置计数的当前值, 并可以作为指令的操作数. 
	# 局部符号用于帮助编译器和编程人员临时使用一些名称. 共有 10 个局部符号名, 可在整个程序中重复使用. 
	# 这些符号名使用名称 '0', '1', ..., '9' 来引用. 为了定义一个局部符号, 需把标号写成 'N:' 形式(其中 N 表示一个数字). 
	# 为了引用先前最近定义的这个符号, 需要写成 'Nb', 其中 N 是定义标号时使用的数字. 
	# 为了引用一个局部标号的下一个定义, 而要与成 'Nf', 这里gdb 调试 32 位 N 是 10 个前向引用之一. 
	# 上面 'b' 表示 "向后(backwards)", 'f' 表示 "向前gdb 调试 32 位(forwards)". 
	# 在汇编程序的某一处, 我们最大可以向后/向前引用 10 个标号.
	movl %eax, 0x000000					# loop forgdb 调试 32 位ever if it isn't
	cmpl %eax, 0x100000
	je 1b								# '1b' 表示向后跳转到标号 1 去. 若是 '5f' 则表示向前跳转到标号 5 去.
/*
 * NOTE! 486 should set bit 16, to check for write-protect in supervisor
 * mode. Then it would be unnecessary with the "verify_area()"-calls.
 * 486 users probably want to set the NE (#5) bit also, so as to use
 * int 16 for math errors.
 */
/*
 * 注意! 在下面这段程序中, 486 应该将位 16 置位, 以检查在超级用户模式下的写保护, 此后 "verify_area()" 调用就不需要了. 
 * 486 的用户通常也会想将 NE(#5) 置位, 以便对数学协处理器的出错使用 int 16.
 *
 */
 # 上面原注释中提到的 486CPU 中 CR0 控制器的位 16 是写保护标志 WP, 用于禁止超级用户级的程序向一般用户只读页面中进行写操作. 
 # 该标志主要用于操作系统在创建新进程时实现写时复制方法.
 # 下面这段程序用于检查数学协处理器芯片是否存在. 
 # 方法是修改控制寄存器 CR0, 在假设存在协处理器的情况下执行一个协处理器指令, 如果出错的话则说明协处理器芯片不存在, 
 # 需要设置 CR0 中的协处理器仿真位 EM(位 2), 并复位协处理器存在标志 MP(位 1).
	movl %cr0, %eax						# check math chip
	andl $0x80000011, %eax				# Save PG,PE,ET
	/* "orl $0x10020,%eax" here for 486 might be good */
	orl $2, %eax						# set MP
	movl %eax, %cr0
	call check_x87
	jmp after_page_tables

/*
 * We depend on ET to be correct. This checks for 287/387.
 */
/*
 * 我们依赖于 ET 标志的正确性来检测 287/387 存在与否.
 *
 */
# 下面 fninit 和 fstsw 是数学协处理器(80287/80387)的指令.
# fninit 向协处理器发出初始化命令, 它会把协处理器置于一个末受以前操作影响的已和状态, 设置其控制字为默认值, 清除状态字和所有浮点栈式寄存器. 
# 非等待形式的这条指令(fninit)还会让协处理器终止执行当前正在执行的任何先前的算术操作. 
# fstsw 指令取协处理器的状态字. 如果系统中存在协处理器的话, 那么在执行了 fninit 指令后其状态字低字节肯定为 0.
check_x87:
	fninit								# 向协处理器发出初始化命令.
	fstsw %ax							# 取协处理器状态字到 ax 寄存器中.
	cmpb $0, %al						# 初始化状态字应该为 0, 否则说明协处理器不存在.
	je 1f								/* no coprocessor: have to set bits */
	movl %cr0, %eax						# 如果存在则向前跳转到标号 1 处, 否则改写 cr0.
	xorl $6, %eax						/* reset MP, set EM */
	movl %eax, %cr0
	ret

# 下面是一个汇编语言指示符. 其含义是指存储边界对齐调整. 
# "2" 表示把随后的代码或数据的偏移位置调整到地址值最后 2 位为零的位置(2 ^ 2), 即按 4 字节方式对齐内存地址. 
# 不过现在 GNU as 直接写出对齐的值而非 2 的次方值了. 使用该指示符的目的是为了提高 32 位 CPU 访问内存中代码或数据的速度和效率.
# 下面的两个字节值是 80287 协处理器指令 fsetpm 的机器码. 其作用是把 80287 设置为保护模式.
# 80387 无需该指令, 并且将会把该指令看作是空操作.

.align 2
1:	.byte 0xDB,0xE4		/* fsetpm for 287, ignored by 387 */	# 287 协处理器码.
	ret

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It then loads
 *  idt. Everything that wants to install itself
 *  in the idt-table may do so themselves. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok. This routine will be over-
 *  written by the page tables.
 */
/*
 * 下面这段是设置中断描述符表子程序 setup_idt: 
 *
 * 将中断描述符表 idt 设置成具有 256 个项, 并都指向 ignore_int 中断门. 然后加载中断描述符表寄存器(lidt 指令). 
 * 真正实用的中断门以后再安装. 当我们在其他地方认为一切都正常时再开启中断. 该子程序将会被页表覆盖掉.
 */
# 中断描述符表中的项虽然也是 8 字节组成, 但其格式与全局表中的不同, 被称为门描述符. 
# 它的 0-1, 6-7 字节是偏移量, 2-3 字节是选择符, 4-5 字节是一些标志. 该描述符, 共 256 项. 
# eax 含有描述符低 4 字节, edx 含有高 4 字节. 内核在随后的初始化过程中会替换安装那些真正实用的中断描述符项.
setup_idt:
	lea ignore_int, %edx			# 将 ignore_int 的有效地址(偏移值)值加载到 eax 寄存器.
	movl $0x00080000, %eax			# 将段选择符 0x08 置入 eax 的高 16 位中.
	movw %dx, %ax					/* selector = 0x0008 = cs */ # 偏移值的低 16 位置入 eax 的低 16 位中. 此时 eax 含有门描述符低 4 字节的值.
	movw $0x8E00, %dx				/* interrupt gate - dpl=0, present */	# 此时 edx 含有门描述符高 4 字节的值.

	lea idt, %edi					# idt 是中断描述符表的地址.
	mov $256, %ecx
rp_sidt:
	movl %eax, (%edi)				# 将哑中断门描述符存入表中.
	movl %edx, 4(%edi)
	addl $8, %edi					# edi 指向表中下一项.
	dec %ecx
	jne rp_sidt
	lidt idt_descr					# 加载中断描述符表寄存器值.
	ret

/*
 *  setup_gdt
 *
 *  This routines sets up a new gdt and loads it.
 *  Only two entries are currently built, the same
 *  ones that were built in init.s. The routine
 *  is VERY complicated at two whole lines, so this
 *  rather long comment is certainly needed :-).
 *  This routine will beoverwritten by the page tables.
 */
/*
 * 设置全局描述符表项 setup_gdt
 * 这个子程序设置一个新的全局描述符表 gdt, 并加载. 该子程序将被页表覆盖掉.
 *
 */
setup_gdt:
	lgdt gdt_descr				# 将 gdt_descr 中的内容加载进全局描述符表寄存器(GDT 中的内容已设置好)
	ret

/*
 * I put the kernel page tables right after the page directory,
 * using 4 of them to span 16 Mb of physical memory. People with
 * more than 16MB will have to expand this.
 */
/*
 * Linus 将内核的内存页表直接放在页目录之后, 使用了 4 个表来寻址 16MB 的物理内存. 
 * 如果你有多于 16MB 的内存, 就需要在这里进行扩充修改.
 */
 # 每个页表长为 4KB 字节(1 页内存页面), 而每个页表项需要 4 个字节, 因此一个页表共可以存放 1024 个表项. 
 # 如果一个页表项寻址 4KB 的地址空间, 则一个页表就可以寻址 4MB 的物理内存.
 # 页表项的格式为: 项的前 0-11 位存放一些标志, 例如是否在内存中(P 位 0), 读写许可(R/W 位 1), 
 # 普通还是超级用户使用(U/S 位 2), 是否修改过了(是否脏了)(D 位 6)等;
 # 表项的位 12-31 是页框地址, 用于指出一页内存的物理起始地址.

.org 0x1000						# 从偏移 0x1000 处开始的第 1 个页表(偏移 0 开始处将存放页表目录).
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000						# 定义下面的内存数据块从偏移 0x5000 处开始.
/*
 * tmp_floppy_area is used by the floppy-driver when DMA cannot
 * reach to a buffer-block. It needs to be aligned, so that it isn't
 * on a 64kB border.
 */
/*
 * 当 DMA(直接存储器访问)不能访问缓冲块时, 下面的 tmp_floppy_area 内存块就可供软盘驱动程序使用. 
 * 其地址需要对齐调整, 这样就不会跨越 64KB 边界.
 */

tmp_floppy_area:
	.fill 1024,1,0				# 共保留 1024 项, 每项 1B, 填充数值 0.

 # 下面这几个入栈操作用于为跳转到 init/main.c 中的 main() 函数作准备工作. 
 # pushl $L6 指令在栈中压入返回地址, 而 pushl $main 则压入了 main() 函数代码的地址. 
 # 当 head.s 最后执行 ret 指令时就会从栈中弹出 main() 函数的地址, 并把控制权转移到 init/main.c 程序中.
 # 前面 3 个入栈 0 值应该分别表示 envp, argv 指针和 argc 的值, 但 main() 没有用到.
after_page_tables:
	pushl $0						# These are the parameters to main :-)
	pushl $0						# 这些是调用 main 程序的参数(指 init/main.c).
	pushl $0						# 其中的 '$' 符号表示这是一个立即操作数.
	pushl $L6						# return address for main, if it decides to.
	pushl $main						# 'main' 是编译程序对 main 的内部表示方法.
	jmp setup_paging				# 跳转至 setup_paging
L6:
	jmp L6							# main should never return here, but
									# just in case, we know what happens.
									# main 程序绝对不应该返回到这里. 
									# 不过为了以防万一, 所以添加了该语句. 这样我们就知道发生什么问题了.

/* This is the default interrupt "handler" :-) */
/* 下面是默认的中断 "向量句柄" */

int_msg:
	.asciz "Unknown interrupt\n\r"	# 定义字符串 "末知中断(回车换行)".

.align 2							# 按 4 字节方式对齐内存地址.
ignore_int:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds						# 这里请注意!! ds, es, fs, gs 等虽然是 16 位寄存器, 
	push %es 						# 但入栈后仍然会以 32 位的形式入栈, 即需要占用 4 个字节的堆栈空间.
	push %fs
	movl $0x10, %eax				# 设置段选择符(使 ds, es, fs 指向 gdt 表中的数据段).
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	pushl $int_msg					# 把调用 printk 函数的参数指针(地址)入栈. 注意! 
									# 若 int_msg 前不加 '$', 则表示把 int_msg 符处的长字('Unkn')入栈.
	call printk						# 该函数在 /kernel/printk.c 中.
	popl %eax
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret							# 中断返回(把中断调用时压入栈的 CPU 标志寄存器(32 位)值也弹出).


/*
 * Setup_paging
 *
 * This routine sets up paging by setting the page bit
 * in cr0. The page tables are set up, identity-mapping
 * the first 16MB. The pager assumes that no illegal
 * addresses are produced (ie >4Mb on a 4Mb machine).
 *
 * NOTE! Although all physical memory should be identity
 * mapped by this routine, only the kernel page functions
 * use the >1Mb addresses directly. All "normal" functions
 * use just the lower 1Mb, or the local data space, which
 * will be mapped to some other place - mm keeps track of
 * that.
 *
 * For those with more memory than 16 Mb - tough luck. I've
 * not got it, why should you :-) The source is here. Change
 * it. (Seriously - it shouldn't be too difficult. Mostly
 * change some constants etc. I left it at 16Mb, as my machine
 * even cannot be extended past that (ok, but it was cheap :-)
 * I've tried to show which constants to change by having
 * some kind of marker at them (search for "16Mb"), but I
 * won't guarantee that's all :-( )
 */
/*
 * 这个子程序通过设置控制寄存器 cr0 的标志(PG 位 31)来启动对内存的分页处理功能, 
 * 并设置各个页表项的内容, 以**恒等映射**前 16MB 的物理内存. 
 * 分页器假定不会产生非法的地址映射(也即在只有 4MB 的机器上设置出大于 4MB 的内存地址).
 *
 * 注意! 尽管所有的物理地址都应该由这个子程序进行恒等映射, 但只有内核页面管理函数能直接使用 > 1MB 的地址. 
 * 所有 "普通" 函数仅使用低于1MB的地址空间, 或者是使用局部数据空间, 
 * 该地址空间将被映射到其他一些地方去 -- mm(内存管理程序)会管理这些事的.
 *
 */
 # 上面英文注释第 2 段的含义是指在机器物理内存中大于 1MB 的内存空间主要被用于主内存区. 
 # 主内存区空间由 mm 模块管理, 它涉及页面映射操作. 内核中所有其它函数就是这里指的一般(普通)函数. 
 # 若要使用主内存区的页面, 就需要使用 get_free_page() 等函数获取. 
 # 因为主内存区中内存页面是共享资源, 必须有进行统一管理以避免资源争用和竞争.
 #
 # 在内存物理地址 0x0 处开始存放 1 页页目录表和 4 页页表. 
 # 页目录表是系统所有进程共用的, 而这里的 4 页页表则属于内核专用, 它们一一映射线性地址起始 16MB 空间范围到物理内存上. 
 # 对于新的进程, 系统会在主内存区为其申请页面存放页表. 另外, 1 页内存长度是 4096 字节.

.align 2								# 按 4 字节方式对齐内存地址边界.
setup_paging:							# 首先对 5 页内存(1 页目录 + 4 页页表)清零.
	movl $1024 * 5, %ecx				/* 5 pages: 1 pg_dir + 4 page tables */
	xorl %eax, %eax
	xorl %edi, %edi						/* pg_dir is at 0x000 */	# 页目录从 0x0000 地址开始
	cld;rep;stosl						# eax 内容存到 es:edi 所指内存位置处, 且 edi 增 4.

	# 下面 4 句设置页目录表中的前 4 项, 因为我们(内核)共有 4 个页表所以只需设置 4 项.
	# 页目录项的结构与页表项的结构一样, 4 个字节为 1 项, 低 12 位为页面属性, 高 20 位(左移 12 位)页帧地址.
	# 例如 "$pg0 + 7" 表示: 0x00001007, 是页目录表中的第 1 项.
	# 则第 1 个页表所在的地址 = 0x00001007 & 0xfffff000 = 0x1000(页帧地址);
	# 第 1 个页表的属性标志 = 0x00001007 & 0x00000fff = 0x007(页面属性), 表示该页存在, 用户可读写.
	movl $pg0 + 7, pg_dir				/* set present bit/user r/w */
	movl $pg1 + 7, pg_dir + 4			/*  --------- " " --------- */
	movl $pg2 + 7, pg_dir + 8			/*  --------- " " --------- */
	movl $pg3 + 7, pg_dir + 12			/*  --------- " " --------- */

	# 下面 6 行填写 4 个页表中所有项的内容, 共有: 
	# 4(页表) * 1024(项/页表) = 4096 项(0 - 0xfff), 即能映射物理内存 4096 * 4KB = 16MB.
	# 每项的内容是: 当前项所映射的物理内存地址 + 该页的属性标志(这里均为 7 - 页存在， 用户可读写).
	# 设置的方法是从最后一个页表的最后一项开始按倒退顺序填写. 一个页表的最后一项在页表中的位置是 1023 * 4 = 4092. 
	# 因此最后一页的最后一项的位置就是 $pg3 + 4092.
	movl $pg3 + 4092, %edi				# ds:edi 指向第 4 个页表的最后一项.
	# 最后一项对应物理内存页的地址是 0xfff000, 加上属性标志 7, 即为 0xfff007.
	movl $0xfff007, %eax				/* 16Mb - 4096 + 7 (r/w user, p) */
	# 0xfff000 -> 0b-11-1111111111-000000000000 ==> 11 是页目录索引值, 其后是页表项索引值, 再其后是属性值.
	std									# 方向位置位, edi 值递减(4 字节).
1:	stosl								/* fill pages backwards - more efficient :-) */
	subl $0x1000, %eax					# 每填好一项, 物理地址值减 0x1000(4KB).
	jge 1b								# eax 如果大于或等于 0 则继续设置, 如果小于 0 则说明全填写好了.
	cld
	# 设置页目录表基地址寄存器 cr3 的值, 指向页目录表. cr3 中保存的是页目录表的物理地址(0x0).
	xorl %eax, %eax						/* pg_dir is at 0x0000 */		# 页目录表在 0x0000 处.
	movl %eax, %cr3						/* cr3 - page directory start */
	# 设置启动使用分页处理(cr0 的 PG 标志, 位 31)
	movl %cr0, %eax 					
	orl $0x80000000, %eax				# 添上 PG 标志.
	movl %eax, %cr0						/* set paging (PG) bit */ 		# 开启分页机制. 
	ret									/* this also flushes prefetch-queue */

# 在改变分页处理标志后要求使用转移指令刷新预取指令队列, 这里用的是返回指令 ret.
# 该返回指令的另一个作用是将 pushl $main 压入堆栈中的 main 程序的地址弹出, 
# 并跳转到 /init/main.c 程序去运行. 本程序到此就真正结束了.

.align 2								# 按 4 字节方式对齐内存地址边界.
.word 0									# 这里先空出 2 字节, 这样. long _idt 的长字是 4 字节对齐的.

# 下面是加载中断描述符表寄存器 idtr 的指令 lidt 要求的 6 字节操作数. 
# 前 2 字节是 idt 表的限长, 后 4 字节是 idt 表在线性地址空间中的 32 位基地址.
idt_descr:
	.word 256 * 8 - 1					# idt contains 256 entries
	.long idt
.align 2
.word 0

# 下面是加载全局描述符表寄存器 gdtr 的指令 lgdt 要求的 6 字节操作数. 
# 前 2 字节是 gdt 表的限长, 后 4 字节是 gdt 表的线性基地址. 
# 这里全局表长度设置为 2KB 字节(0x7ff 即可), 因为每 8 字节组成一个描述符项, 所以表中共可有 256 项. 
# 符号 gdt 是全局表在本程序中的偏移位置.

gdt_descr:
	.word 256 * 8 - 1					# so does gdt (not that that's any
	.long gdt							# magic number, but it works for me :^)

	.align 8							# 按 8(2^3) 字节方式对齐内存地址边界.
idt:	.fill 256, 8, 0					# idt is uninitialized	# 256 项, 每项 8 字节, 填 0.

# 全局表, 前 4 项分别是空项(不用), 代码段描述符, 数据段描述符, 系统调用段描述符, 
# 其中系统调用段描述符并没有派用处, Linus 当时可能曾想把系统调用代码专门放在这个独立的段中.
# 同还预留了 252 项的空间, 用于放置所创建任务的局部描述符(LDT)和对应的任务状态段 TSS 的描述符.
# (0-nul, 1-cs, 2-ds, 3-syscall, 4-TSS0, 5-LDT0, 6-TSS1, 7-LDT1, 8-TSS2 etc...)
gdt:
	.quad 0x0000000000000000			/* NULL descriptor */
    # 一致性标志 C = 0, 非一致性代码段, 用户态代码要想访问该代码段只能通过调用门(比如陷阱门 int 0x80)
	.quad 0x00c09a0000000fff			/* 16Mb */ # 0x08, 内核代码段最大长度 16MB. DPL = 0x9 = 0b-1-00-1, 即 DPL = 00.
	.quad 0x00c0920000000fff			/* 16Mb */ # 0x10, 内核数据段最大长度 16MB. DPL = 0x9 = 0b-1-00-1, 即 DPL = 00.
	.quad 0x0000000000000000			/* TEMPORARY - don't use */
	.fill 252, 8, 0						/* space for LDT's and TSS's etc */		# 预留空间.