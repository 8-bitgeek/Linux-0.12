/*
 *  linux/fs/exec.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * #!-checking implemented by tytso.
 */
/*
 * #! 开始脚本程序的检测代码部分是由 tytso 实现的.
 */

/*
 * Demand-loading implemented 01.12.91 - no need to read anything but
 * the header into memory. The inode of the executable is put into
 * "current->executable", and page faults do the actual loading. Clean.
 *
 * Once more I can proudly say that linux stood up to being changed: it
 * was less than 2 hours work to get demand-loading completely implemented.
 */
/*
 * 需求时加载实现于 1991.12.1 - 只需将执行文件头部读进内存而无须将整个执行文件都加载进内存. 
 * 执行文件的 i 节点被放在当前进程的可执行字段中 "current->executable",
 * 页异常会进行执行文件的实际加载操作. 这很完美.
 *
 * 我可以再一次自豪地说, linux 经得起修改: 只用了不到 2 小时的工作就完全实现了需求加载处理.
 */

//#include <signal.h>					// 信息头文件. 定义信号符号常量, 信号结构及信号操作函数原型.
#include <errno.h>						// 错误号头文件. 包含系统中各种出错号.
#include <string.h>
#include <sys/stat.h>					// 文件状态头文件. 含有文件状态结构 stat{} 和符号常量等.
#include <a.out.h>						// a.out 头文件. 定义了 a.out 执行文件格式和一些宏.

#include <linux/fs.h>					// 文件系统头文件. 定义文件表结构(file, m_inode)等.
#include <linux/sched.h>				// 调度程序头文件, 定义了任务结构 task_struct, 任务 0 数据等.
//#include <linux/kernel.h>				// 内核头文件. 含有一些内核常用函数的原型定义.
//#include <linux/mm.h>					// 内存管理头文件. 含有页面大小定义和一些页面释放函数原型.
#include <asm/segment.h>				// 段操作头文件. 定义了有关段寄存器操作的嵌入式汇编函数.

extern int sys_exit(int exit_code);
extern int sys_close(int fd);

/*
 * MAX_ARG_PAGES defines the number of pages allocated for arguments
 * and envelope for the new program. 32 should suffice, this gives
 * a maximum env+arg of 128kB !
 */
/*
 * MAX_ARG_PAGES 定义了为新程序分配的给参数和环境变量使用的最大内存页数. 
 * 32 页内存应该足够了, 这使得环境和参数(env + arg)空间的总和达到 128KB!
 */
#define MAX_ARG_PAGES 32

// 使用库文件系统调用. 
// 参数: library - 库文件名. 
// 为进程选择一个库文件, 并替换进程当前库文件 i 节点字段值为这里指定库文件名的 i 节点指针. 
// 如果 library 指针为空, 则把进程当前的库文件释放掉. 
// 返回: 成功返回 0, 否则返回出错码. 
int sys_uselib(const char * library)
{
	struct m_inode * inode;
	unsigned long base;

	// 首先判断当前进程是否普通进程. 这是通过查看当前进程的空间长度来做到的. 
	// 因为普通进程的空间长度被设置为 TASK_SIZE(64MB). 
	// 因此若进程逻辑地址空间长度不等于 TASK_SIZE 则返回出错码(无效参数). 
	// 否则取库文件 i 节点 inode. 若库文件名指针空, 则设置 inode 等于 NULL. 
	if (get_limit(0x17) != TASK_SIZE)
		return -EINVAL;
	if (library) {
		if (!(inode = namei(library)))							/* get library inode */
			return -ENOENT;                 					/* 取库文件 i 节点 */
	} else
		inode = NULL;
	/* we should check filetypes (headers etc), but we don't */
	/* 我们应该检查一下文件类型(如头部信息等), 但是我们还没有这样做. */
	// 然后放回进程原库文件 i 节点, 并预置进程库 i 节点字段为空. 
	// 接着取得进程的库代码所在位置, 并释放原库代码的页表所占用的内存页面. 
	// 最后让进程库 i 节点字段指向新库 i 节点, 并返回 0(成功). 
	iput(current->library);
	current->library = NULL;
	base = get_base(current->ldt[2]);
	base += LIBRARY_OFFSET;
	free_page_tables(base, LIBRARY_SIZE);
	current->library = inode;
	return 0;
}

/*
 * create_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */
/*
 * create_tables() 函数在新任务内存中解析环境变量和参数字符串, 由此创建指针表, 
 * 并将它们的地址放到 "栈" 上, 然后返回新栈的指针值.
 */
// 在新任务中创建参数和环境变量指针表.
// 参数: p - 数据段中参数和环境空间偏移指针; argc - 参数个数; envc - 环境变量个数.
// 返回: 栈指针值.
static unsigned long * create_tables(char * p, int argc, int envc)
{
	unsigned long * argv, * envp;
	unsigned long * sp;

	// 栈指针是以 4 字节为边界进行寻址的, 因此这里需让 sp 为 4 的整数倍值. 此时 sp 位于参数环境表的末端. 
	// 然后我们先把 sp 向下(低地址方向)移动, 在栈中空出环境变量指针占用的空间, 并让环境变量指针 envp 指向该处. 
	// 多空出的一个位置用于在最后存放一个 NULL 值. 下面指针加 1, sp 将递增指针宽度字节值(4 字节). 
	// 再把 sp 向下移动, 空出命令行参数指针占用的空间, 并让 argv 指针指向该处. 
	// 同样, 多空处的一个位置用于存放一个 NULL 值. 此时 sp 指向参数指针块的起始处, 
	// 我们将环境参数块指针 envp 和命令行参数块指针以及命令行参数个数值分别压入栈中.
	sp = (unsigned long *) (0xfffffffc & (unsigned long) p);  // 使 sp 指向 4 字节边界.
	sp -= envc + 1;
	envp = sp;
	sp -= argc + 1;
	argv = sp;
	// 经过上面的操作后的参数和环境空间: | -- 参数和环境空间剩余部分 -- (sp ->)| 存放命令行参数指针 | NULL | 存放环境变量指针 | NULL | arg | env | <- 参数和环境空间末端
	put_fs_long((unsigned long)envp, --sp);
	put_fs_long((unsigned long)argv, --sp);
	put_fs_long((unsigned long)argc, --sp);
	// 经过上面的操作后的参数和环境空间: | -- 参数和环境空间剩余部分 -- (sp ->) | argc | 命令行参数指针的指针 | 环境变量指针的指针 | 存放命令行参数指针(s) | NULL | 存放环境变量指针(s) | NULL | arg | env | <- 参数和环境空间末端
	// 再将命令行各参数指针和环境变量各指针分别放入前面空出来的相应地方, 最后分别放置一个 NULL 指针.
	while (argc-- > 0) {
		put_fs_long((unsigned long) p, argv++);
		while (get_fs_byte(p++)) /* nothing */ ;	// p 指针指向下一个参数串.
	}
	put_fs_long(0, argv);
	while (envc-- > 0) {
		put_fs_long((unsigned long) p, envp++);
		while (get_fs_byte(p++)) /* nothing */ ;	// p 指针指向下一个参数串.
	}
	put_fs_long(0, envp);
	return sp;										// 返回构造的当前新栈指针.
}

/*
 * count() counts the number of arguments/envelopes
 */
/*
 * count() 函数计算命令行参数/环境变更的个数.
 */
// 计算参数个数.
// 参数: argv - 参数指针数组, 最后一个指针项是 NULL(每个指针占 4 个字节).
// 统计参数指针数组中指针的个数.
// 返回: 参数个数.
static int count(char ** argv)
{
	int i = 0;
	char ** tmp;

	if (tmp = argv)
		while (get_fs_long((unsigned long *) (tmp++))) 		// 如果获取到的地址不为 NULL, 则说明没到最后一个参数.
			i++;

	return i;
}

/*
 * 'copy_string()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 *
 * Modified by TYT, 11/24/91 to add the from_kmem argument, which specifies
 * whether the string and the string array are from user or kernel segments:
 *
 * from_kmem     argv *        argv **
 *    0          user space    user space
 *    1          kernel space  user space
 *    2          kernel space  kernel space
 *
 * We do this by playing games with the fs segment register.  Since it
 * it is expensive to load a segment register, we try to avoid calling
 * set_fs() unless we absolutely have to.
 */
/*
 * 'copy_string()' 函数从用户内存空间复制参数/环境字符串到内核空闲页面中. 这些已具有直接放到新用户内存中的格式.
 *
 * 由 TYT(Tytso) 于 1991.11.24 日修改, 增加了 from_kmem 参数, 
 * 该参数指明了字符串或字符串数组是来自用户段还是内核段.
 *
 * from_kmem     指针 *argv    字符串 **argv
 *    0          用户空间		用户空间
 *    1          内核空间		用户空间
 *    2          内核空间		内核空间
 *
 * 我们是通过巧妙处理 fs 段寄存器来操作的. 
 * 由于加载一个段寄存器代价太高, 所以我们尽量避免调用 set_fs(), 除非实在必要.
 */
// 复制指定个数的参数字符串到参数和环境空间中.
// 参数: argc - 欲添加的参数个数; argv - 参数字符串指针数组; page - 参数和环境空间页面指针数组. 
// 		p - 参数表空间中偏移指针, 始终指向已复制串的头部; from_kmem - 字符串来源标志. 
// 在 do_execve() 函数中, p 初始化为指向参数表(128KB)空间的最后一个长字处, 
// 参数字符串是以堆栈操作方式逆向往其中复制存放的. 
// 因此 p 指针会随着复制信息的增加而逐渐减小, 并始终指向参数字符串的头部. 
// 字符串来源标志 from_kmem 应该是 TYT(创作者)为了给 execve() 增添执行脚本文件的功能而新加的参数. 
// 当没有运行脚本文件的功能时, 所有参数字符串都在用户数据空间中. 
// 返回: 参数和环境空间中当前的指针. 若出错则返回 0.
static unsigned long copy_strings(int argc, char ** argv, unsigned long * page,
		unsigned long p, int from_kmem)
{
	char * tmp, * pag;
	int len, offset = 0;
	unsigned long old_fs, new_fs;

	if (!p) 										// p == 0 表示参数和环境空间没有内存了.
		return 0;									/* bullet-proofing */	/* 偏移指针验证 */
	// 内核数据段作为 new_fs, 用户数据段作为 old_fs. 
	// 如果字符串和字符串数组(指针)来自内核空间, 则设置 fs 段寄存器指向内核数据段.
	new_fs = get_ds();								// 获取当前 ds 寄存器的值. 系统调用时 ds 指向内核数据段(0x10).
	old_fs = get_fs(); 								// 获取当前 fs 寄存器的值. 系统调用时 fs 指向任务局部数据段(0x17).
	if (from_kmem == 2)								// 若字符串指针和字符串都在内核空间则设置 fs 指向内核空间.
		set_fs(new_fs);
	// 然后循环处理各个参数, 从最后一个参数逆向开始复制, 复制到指定偏移地址处. 在循环中, 首先取需要复制的当前字符串指针. 
	// 如果字符串在用户空间而字符串数组(字符串指针)在内核空间, 则设置 fs 段寄存器指向内核数据段(ds). 
	// 并在内核数据空间中取了字符串指针 tmp 之后就立刻恢复 fs 段寄存器原值(fs 再指回用户空间). 
	// 否则不用修改 fs 值而直接从用户空间取字符串指针到 tmp.
	while (argc-- > 0) {
		if (from_kmem == 1)							// 若参数字符串指针在内核空间, 字符串在用户空间, 则让 fs 先指向内核空间来读取字符串指针.
			set_fs(new_fs);
		// tmp 指向参数字符串数组中的某一个(开始时指向最后一个字符串, 每次循环都向前一个字符串移动).
		if (!(tmp = (char *) get_fs_long(((unsigned long *) argv) + argc)))
			panic("argc is wrong");
		if (from_kmem == 1)							// 若参数字符串指针在内核空间, 字符串在用户空间, 则让 fs 再指回用户空间, 下面要读取字符串了.
			set_fs(old_fs);
		// 如果 from_kmem == 0(参数字符串及其指针均在进程内存空间中), 那么 fs 仍然指向进程局部数据段.
		// 从用户空间读取当前参数字符串, 并计算其长度 len. 此后 tmp 指向该字符串末端. 
		len = 0;									/* remember zero-padding */
		do {										/* 我们知道串是以 NULL 字节结尾的 */
			len++;
		} while (get_fs_byte(tmp++)); 				// 循环结束后 tmp 指向当前参数字符串的末尾.
		// 如果环境和参数空间不够再放下这个字符串, 则还原 fs, 并返回 0.
		if (p - len < 0) {							/* this shouldn't happen - 128kB */
			set_fs(old_fs);							/* 不会发生 -- 因为有 128KB 的空间 */
			return 0;
		}
		// 然后逐个字符把字符串复制到参数和环境空间中.
		// 从最后一个页面(*page[PAGE_SIZE])末尾向页面头开始写, 也即先写字符串的最后一个字符. 
		// 在循环复制字符串的字符过程中, 我们首先要判断参数和环境空间中相应位置处是否已经有内存页面. 
		// 如果还没有就先为其申请 1 页内存页面. 偏移量 offset 被用作当前在一个页面中的指针. 
		// 因为刚开始执行本函数时, 偏移量 offset 被初始化为 0, 
		// 所以(offset - 1 < 0)肯定成立而使得 offset 重新被设置为当前 p 指针在页面范围内的偏移值.
		while (len) {
			--p; --tmp; --len;
			if (--offset < 0) { 					// 当前指针如果小于 0, 那么重置该指针.
				offset = p % PAGE_SIZE; 			// 设置 offset 为页内偏移量.
				// 若参数字符串和其指针都在内核空间则 fs 指回用户空间(有可能要调用 get_free_page(), 所以需要先将 fs 改回去).
				if (from_kmem == 2)
					set_fs(old_fs);
				// 如果当前参数指针 p 所在的参数和环境页面地址 page[p/PAGE_SIZE] == 0, 
				// 则表示这个内存页面不存在, 则需要申请一页空闲内存页, 并将该页面地址填入地址列表 page[] 中, 
				if (!(pag = (char *) page[p / PAGE_SIZE]) && 		
				    !(pag = (char *) (page[p / PAGE_SIZE] = get_free_page())))
					return 0;
				if (from_kmem == 2)					// 然后再将 fs 改回来, 指向内核空间.
					set_fs(new_fs);

			}
			// 然后从 fs 段中复制字符串的 1 字节到参数和环境内存页面 pag 的 offset 处.
			*(pag + offset) = get_fs_byte(tmp);
		}
	}
	// 如果字符串和字符串数组在内核空间, 则恢复 fs 段寄存器原值. 
	// 最后, 返回参数和环境空间中已复制参数的头部偏移值.
	if (from_kmem == 2)
		set_fs(old_fs);
	return p;
}

// 修改任务的局部描述符表内容.
// 修改局部描述符表 LDT 中描述符的段基址和段限长, 并将参数和环境空间页面放置在数据段末端.
// 参数: text_size - 执行文件头部中 a_text 字段给出的代码段长度值; page - 参数和环境空间页面指针数组.
// 返回: 数据段限长值(64MB)
static unsigned long change_ldt(unsigned long text_size, unsigned long * page)
{
	unsigned long code_limit, data_limit, code_base, data_base;
	int i;

	// 首先把代码和数据段长度均设置为 64MB. 然后取当前进程局部描述符表代码段描述符中代码段基址. 
	// 代码段基址与数据段基址相同. 再使用这些新值重新设置局部表中代码段和数据段描述符中的基址和段限长. 
	// 这里请注意, 由于被加载的新程序的代码和数据段基址与原程序的相同, 因此没有必要再重复设置它们, 
	code_limit = TASK_SIZE; 						// 64MB.
	data_limit = TASK_SIZE;
	code_base = get_base(current->ldt[1]); 			// 获取当前进程的代码段基地址(线性地址).
	data_base = code_base;
	set_base(current->ldt[1], code_base); 			// 由于新的代码段基地址与之前一致, 可省略.
	set_limit(current->ldt[1], code_limit); 		// 设置代码段限长为 64MB.
	set_base(current->ldt[2], data_base);			// 由于新的数据段基地址与之前一致, 可省略.
	set_limit(current->ldt[2], data_limit);
	/* make sure fs points to the NEW data segment */
	/* 要确保 fs 段寄存器已指向新的数据段 */
	// fs 段寄存器中放入局部表数据段描述符的选择符(0x17). 即默认情况下 fs 都指向任务数据段.
	__asm__("pushl $0x17; pop %%fs" : :);
	// 然后将参数和环境空间已存放数据的页面(最多有 MAX_ARG_PAGES 页, 128KB)放到数据段末端. 
	// 方法是从进程空间库代码位置开始处往前一页一页地放. 库文件代码占用进程空间最后 4MB. 
	// 函数 put_dirty_page() 用于把物理页面映射到进程逻辑(线性)空间中. 在 mm/memory.c 中.
	data_base += data_limit - LIBRARY_SIZE; 			// LIBRARY_SIZE = 4MB, data_base 此时指向当前进程线性地址空间的末尾 4MB 开始处.
	for (i = MAX_ARG_PAGES - 1; i >= 0; i--) {
		data_base -= PAGE_SIZE; 						// 将参数和环境空间放到库文件代码前面(低地址处).
		if (page[i])									// 若该页面存在, 就放置该页面.
			put_dirty_page(page[i], data_base); 		// 把物理页面地址 page[i] 映射到当前进程的线性空间 data_base 中.
	}
	return data_limit;									// 最后返回数据段限长(64MB).
}

/*
 * 'do_execve()' executes a new program.
 *
 * NOTE! We leave 4MB free at the top of the data-area for a loadable
 * library.
 */
/*
 * 'do_execve()' 函数执行一个新程序.
 */
// execve() 系统中断调用函数(kernel/sys_call.s #sys_execve). 加载并执行子进程(其他程序).
// 该函数是系统中断调用(int $0x80)功能号 __NR_execve 调用的函数. 
// 函数的参数是进入系统调用处理过程后直至调用本函数之前逐步压入栈中的值.  
// 这些值包括:
// 1: system_call 在调用系统调用函数前入栈的 edx, ecx 和 ebx 寄存器值, 分别对应 **envp, **argv 和 *filename;
// 2: system_call 在调用 sys_call_table 中 sys_execve 函数(指针)时(call sys_execve)压入栈的函数返回地址(tmp);
// 3: sys_execve 在调用本函数 do_execve 前入栈的指向栈中调用系统中断的程序代码指针 eip.
// 参数:
// eip - sys_execve 在调用本函数前入栈的用户态代码调用 `int $0x80` 的下一行代码指针. 
// 		即系统调用返回后要执行的用户代码的地址.
// 		最后入栈的参数 eip 在参数列表的第一个(实际上最后入栈了一个返回地址, 但是不作为参数).
// 		用户态在调用 `int $0x80` 时, 会发生特权级变化, 同时进行堆栈切换, 
// 		因此会入栈 cs 和 eip, eip[1] = cs(先入栈 cs, 再入栈 eip).
// tmp - system_call 在调用 sys_execve 时的返回地址, 无用(仅在函数返回时被 cpu 自动使用).
// filename - 要执行的程序文件名指针;
// argv - 命令行参数指针数组的指针;
// envp - 环境变量指针数组的指针.
// 返回: 如果调用成功, 则不返回; 否则设置出错号, 并返回 -1.
int do_execve(unsigned long * eip, long tmp, char * filename,
			  char ** argv, char ** envp) 						// 先入栈的参数在参数列表的后面.
{
	struct m_inode * inode;
	struct buffer_head * bh;
	struct exec ex;
	unsigned long page[MAX_ARG_PAGES];							// 参数和环境变量内存页面的地址指针数组.
	int i, argc, envc;
	int e_uid, e_gid;											// 有效(effective)用户 ID 和有效组 ID.
	int retval;
	// sh - hash 即 '#', bang - ‘！’ 脚本文件以 '#!' 开头, 称为 shebang 或者 hash-bang.
	int sh_bang = 0;											// 控制是否需要执行脚本程序. 置位表示禁止再次执行脚本处理代码. 用于执行脚本时递归调用的逻辑判断.
	unsigned long p = PAGE_SIZE * MAX_ARG_PAGES - 4;			// p 指向参数和环境空间的最后一个长字(4k * 32 - 4).

	// 在内核中打印要执行的文件的名字.
	char s, filename1[128]; 									// 函数内的变量都是在栈中(当前进程的内核态堆栈中).
	int index = 0;
	// 将用户数据空间中的 filename 复制到内核的数据段(filename1)中.
	while (1) {
		// 此时代码运行在内核态, 所以数据段寄存器 ds 指向的是内核的数据段, 这时访问 filename 地址处的内存拿到的不是用户态进程内存里的数据.
		// 不过目前当前进程 0-640KB 这部分映射关系是复制的内核页表的(看下 TASK(0->1) 的 fork 过程便知), 
		// 所以现在当前进程的 fs 在 0-640KB 的逻辑空间也是指向内核数据段(物理地址的 0-640KB), 
		// 后面的 free_page_table 会替换掉 0-640KB 这个映射关系.
		// 参照: sys_call.s # sys_fork -> copy_process() -> copy_mem() -> copy_page_table() (kernel/fork.c)
		s = get_fs_byte(filename + index); 		// 复制用户数据段中的数据到内核的数据段中.
		if (s) {
			*(filename1 + index) = s; 			// 拷贝内核数据段中的 filename 到用户进程内核栈 filename1 (当前进程的内核态栈)中.
			index++;
		} else {
			break;
		}
	}
	*(filename1 + index + 1) = '\0'; 							// 填充字符串结束符.
	Log(LOG_INFO_TYPE, "<<<<< process pid = %d, do_execve: %s >>>>>\n", current->pid, filename1);

	// 在正式设置可执行文件的运行环境之前, 让我们先干些杂事. 
	// 参数 eip[1] 是调用进程的代码段寄存器 CS 值(特权级变化导致堆栈切换时压入内核态堆栈的内容, 见 CLK 图 4-29, p123), 
	// CS 段选择符必须是当前任务的代码段选择符(0x000f), 若不是, 那么 CS 只可能是内核代码段的选择符 0x0008. 
	// 但这是绝对不允许的, 因为内核代码是常驻内存而不能被替换掉的. 
	if ((0xffff & eip[1]) != 0x000f) 					// 当前任务(current)的局部代码段选择符必须是 0x0f(用户态).
		panic("execve called from supervisor mode!");
	// 内核为进程准备了 128KB(32 个页面) 空间来存放要执行文件的命令行参数和环境字符串.
	// 在初始参数和环境空间的操作过程中, **p 将用来指明当前在 128KB 空间中的位置**.
	// 然后再初始化 128KB 的参数和环境空间, 把所有字节清零, 并取出执行文件的 i 节点. 
	// 再根据参数分别计算出命令行参数和环境字符串的个数 argc 和 envc. 另外, 执行文件必须是常规文件.
	for (i = 0; i < MAX_ARG_PAGES; i++)					/* clear page-table */
		page[i] = 0; 									// 清空参数及环境变量页面地址指针列表.
	// 将要执行的文件的 inode 信息读取出来.
	if (!(inode = namei(filename)))						/* get executables inode */
		return -ENOENT;
	argc = count(argv);									// 命令行参数个数.
	envc = count(envp);									// 环境字符串变量个数.

restart_interp:											// 脚本文件处理完后, 使用这个标号重启解释程序.
	if (!S_ISREG(inode->i_mode)) {						/* must be regular file */
		retval = -EACCES;
		goto exec_error2;								// 若不是常规文件则置出错码, 跳转 exec_error2.
	}
	// 下面检查当前进程是否有权运行指定的执行文件. 即根据执行文件 i 节点中的属性, 看看当前进程是否有权执行它. 
	// 先检查文件 inode 属性中是否设置了 set-user-id(执行时设置用户 id, i_mode 位 9 置位)标志和 
	// set-group-id(执行时设置组 id, i_mode 位 10 置位)标志. 
	// 这两个标志主要是让普通用户也能够执行特权用户(比如 root)的程序, 例如执行 passwd 来修改密码等. 
	// 如果执行文件的 set-user-id 或 set-group-id 置位, 则使用执行文件的宿主 uid/gid 来当作执行时的 euid/egid.
	// 否则使用成当前进程的 euid/egid. 这里暂时把这两个判断出来的值保存在变量 e_uid 和 e_gid 中.
	i = inode->i_mode;
	e_uid = (i & S_ISUID) ? inode->i_uid : current->euid; 		// 如果文件的 set-user-id 置位, 则以文件宿主用户执行.
	e_gid = (i & S_ISGID) ? inode->i_gid : current->egid;
	// 如果执行文件属于当前用户(当前进程宿主), 则把文件属性值 i 右移 6 位, 此时最低 3 位是文件宿主的访问权限标志. 
	// 否则的话如果执行文件与当前进程的用户属于同组, 则使属性最低 3 位是执行文件组用户的访问权限标志. 
	// 如果即不是宿主也不是组员, 此时属性字最低 3 位就是其他用户访问该执行文件的权限. 
	// 然后我们根据属性 i 的最低 3 位值来判断当前进程是否有权限运行这个执行文件.
	if (current->euid == inode->i_uid)				// 如果执行文件的宿主是当前用户, 则检查宿主的访问权限.
		i >>= 6; 									// rwx-rwx-rwx: 宿主-组员-其它.
	else if (in_group_p(inode->i_gid)) 				// 如果当前用户与文件宿主在同一个用户组, 则检查组员权限.
		i >>= 3;
	// 此时已经计算出当前用户属于什么用户(宿主/组员/other), 且 i 低 3 位就是这类用户对应的权限.
	// 如果用户不具有可执行权限, 并且文件也不可执行(rwx-rwx-rwx 所有的 x 位都复位), 用户也不是超级用户, 
	// 则跳转到 exec_error2 执行. 换言之, 如果用户有可执行权限, 或者用户是超级用户的情况下, 文件有可执行用户, 那么都可以执行这个文件.
	if (!(i & 1) && !((inode->i_mode & 0111) && suser())) { 
		retval = -ENOEXEC;
		goto exec_error2;
	}
	// 程序执行到这里, 说明当前进程有权运行这个可执行文件.
	// 所以我们需要取出执行文件头部数据并根据其中的信息来分析设置运行环境, 或者运行另一个 shell 程序来执行脚本程序. 
	// 首先读取执行文件第 1 块数据到高速缓冲块中. 并复制缓冲块数据到 ex 中. 
	if (!(bh = bread(inode->i_dev, inode->i_zone[0]))) { 			// 读取文件的第一个数据块.
		retval = -EACCES;
		goto exec_error2;
	}
	// 读取可执行文件头信息.
	ex = *((struct exec *) bh->b_data);								/* read exec-header */
	/* 
	  如果执行文件是脚本文件(以 '#!' 开头), 我们需要读取脚本文件中的内容, 获取其解释程序(比如 /bin/sh)及后面的参数(如果有的话),
	  然后将这些参数和脚本文件名放到执行文件(此时是解释程序)的命令行参数和环境变量空间中(page[]). 
	  在这之前我们需要先把函数指定的原有命令行参数和环境字符串(argv, envp)放到 128KB 空间中, 
	  而这里建立起来的命令行参数则放到它们前面位置处(因为是逆向放置). 
	  设置好解释程序的脚本文件名及参数环境后, 取出解释程序的 i 节点并跳转到 restart_interp 执行解释程序.
	*/
	// 处理完脚本文件之后需要设置一个禁止再次执行下面的脚本处理的标志 sh_bang.
	// 在后面的代码中该标志也用来表示我们已经设置好执行文件的命令行参数, 不要重复设置.
	if ((bh->b_data[0] == '#') && (bh->b_data[1] == '!') && (!sh_bang)) { 	// 如果是脚本文件, 并且还没有处理过这个脚本.
		/*
		 * This section does the #! interpretation.
		 * Sorta complicated, but hopefully it will work.  -TYT
		 */
        /* 这部分处理对 “#!” 的解释, 有些复杂, 但希望能工作.  -TYT */
		char buf[128], *cp, *interp, *i_name, *i_arg;
		unsigned long old_fs;

		// 从这里开始, 我们从脚本文件中提取解释程序名及其参数, 并把解释程序名, 
		// 解释程序的参数和脚本文件名组合放入环境参数块中. 
		// 首先复制脚本文件 0-127 的字节到 buf 中(即跳过开头的 ‘#!’), 
		// 其中含有脚本解释程序名(例如 /bin/sh), 也可能还包含解释程序的几个参数. 
		// 然后对 buf 中的内容进行处理. 删除开始的空格, 制表符. 
		strncpy(buf, bh->b_data + 2, 127); 					// 跳过开头的 '#!' 后读取 127 个字节到 buf 中.
		brelse(bh);             							// 释放缓冲块并放回脚本文件 i 节点. 
		iput(inode);
		buf[127] = '\0';
		if (cp = strchr(buf, '\n')) { 						// 获取第一个 ‘\n’ 的地址, 并替换成 ‘\0’.
			*cp = '\0';     								// 将第 1 个换行符换成 NULL.
			for (cp = buf; (*cp == ' ') || (*cp == '\t'); cp++); // cp 指向 buf 中的第一个非空格和制表符的字符.
		}
		if (!cp || *cp == '\0') {       					// 若该行没有其他内容, 则出错. 
			retval = -ENOEXEC; 								/* No interpreter name found */
			goto exec_error1;       						/* 没有找到脚本解释程序名 */
		}
		// 此时我们得到脚本解释程序的路径名(比如 '/bin/sh').
		interp = i_name = cp; 							 	// 指向解释程序的路径名.
		i_arg = 0;
		for ( ; *cp && (*cp != ' ') && (*cp != '\t'); cp++) { // 循环结束后 cp 指向空格或制表符(如果有的话).
 			if (*cp == '/')
				i_name = cp + 1;							// 循环结束后 i_name 指向解释程序的文件名(非路径名). 
		}
		// 若解释程序文件名后还有字符, 则它们应该是解释程序的参数串, 令 i_arg 指向参数字符串. 
		if (*cp) { 											// 如果 cp 此时没有指向结束符 '\0' 表示此时指向空格或制表符, 表示后面可能还有字符.
			*cp++ = '\0';           						// 解释程序名后添加 NULL 字符, 让 i_name 指向解释文件名. 同时 cp 指向后一个字符.
			i_arg = cp;             						// i_arg 指向解释程序后面的参数. 
		}
		/* OK, we've parsed out the interpreter name and (optional) argument. */
		// 现在我们要把上面解析出来的解释程序文件名 i_name 及其参数 i_arg 和脚本文件名作为解释程序的参数放进环境和参数内存页中. 
		// 首先把函数提供的参数和环境变量字符串先放到环境和参数内存页, 然后再复制脚本中解析出来的. 
		// 例如对于命令行参数来说, 如果原来的参数是 "-arg1 -arg2", 解释程序名是 "bash", 其参数是 "-iarg1 -iarg2", 
		// 脚本文件名(即原来的执行文件名)是 "example.sh", 那么在放入这里的参数之后, 新的命令行类似于这样: 
		//              "bash -iarg1 -iarg2 example.sh -arg1 -arg2"
		// 注意: 这里指针 p 随着复制信息增加而逐渐向小地址方向移动.

		// 首先把函数设置的参数和环境变量复制到进程的环境和参数内存页中.
		// 这里我们把 sh_bang 标志 +1, 然后把函数参数提供的原有参数和环境字符串放入到空间中. 
		// 环境字符串和参数个数分别是 envc 和 argc - 1 个. 少复制的一个原有参数是原来的执行文件名, 即这里的脚本文件名. 
		if (sh_bang++ == 0) { 									// 如果是在解析脚本文件(sh_bang ==0), 将 sh_bang +1 .
			p = copy_strings(envc, envp, page, p, 0); 			// 0 表示 envp 和要复制到的目的地 *page[] 都是用户空间的内存地址.
			p = copy_strings(--argc, argv + 1, page, p, 0); 	// 将环境和参数复制到进程的环境和参数内存页中.
		}
		/*
		 * Splice in (1) the interpreter's name for argv[0]
		 *           (2) (optional) argument to interpreter
		 *           (3) filename of shell script
		 *
		 * This is done in reverse order, because of how the
		 * user environment and arguments are stored.
		 */
        /*
         * 拼接 (1)argv[0] 		中放解释程序的名称. 
         *     (2) (可选的) 	解释程序的参数. 
         *     (3) 			   脚本程序的名称. 
         *
         * 这是以逆序(从高地址向低地址)进行处理的, 是由于用户环境和参数的存放方式造成的. 
         */
		// 复制脚本文件名, 解释程序的参数和解释程序文件名到参数和环境空间中. 
		// from_kmem 表示 filename 在进程数据段中, 但是 filename 的指针保存在内核空间中, 
		p = copy_strings(1, &filename, page, p, 1); 		// 将脚本文件名复制到参数和环境内存空间.
		argc++;
		if (i_arg) {            							// 复制脚本中给解释程序带的参数(上面解析的那些).
			p = copy_strings(1, &i_arg, page, p, 2);		// 2 表示参数字符串和其指针都在内核的数据段中.
			argc++;
		}
		p = copy_strings(1, &i_name, page, p, 2); 			// 复制解释程序的文件名(非路径名)
		argc++;
		if (!p) {
			retval = -ENOMEM;
			goto exec_error1;
		}
		/* OK, now restart the process with the interpreter's inode. */
	    /* OK, 现在使用解释程序的 i 节点重启进程. */
		// 最后我们取得解释程序的 i 节点指针, 然后跳转到 restart_interp 去执行解释程序. 
		// 为了获得解释程序的 i 节点, 我们需要使用 namei() 函数, 
		// 但是该函数所使用的参数(文件名)是从用户数据空间得到的, 即使用的是 fs 段寄存器. 
		// 因此在调用 namei() 函数之前我们需要先临时让 fs 指向内核数据空间, 
		// 使函数能从内核空间得到解释程序名, 并在 namei() 返回后恢复 fs. 
		// 最后跳转到 restart_interp 执行脚本文件指定的解释程序.
		old_fs = get_fs();
		set_fs(get_ds());										// 先让 fs 临时指向内核数据段(0x10).
		if (!(inode = namei(interp))) { 						/* get executables inode */
			set_fs(old_fs);       								/* 更新 inode 为解释程序的 inode */
			retval = -ENOENT;
			goto exec_error1;
		}
		set_fs(old_fs); 										// 恢复 fs 指向进程的局部数据段(0x17).
		goto restart_interp;
    }
	// 此时缓冲块中的执行文件头结构已经复制到了 ex 中. 于是先释放该缓冲块, 并开始对 ex 中的执行头信息进行判断处理. 
	// 对于 Linux0.12 内核来说, 它仅支持 ZMAGIC 执行格式, 并且执行文件代码都从逻辑地址 0 开始执行, 
	// 因此不支持含有代码或数据重定位信息的执行文件. 当然, 如果执行文件实在太大或者执行文件残缺不全, 那么我们也不能运行它. 
	// 因此对于下列情况将不执行程序: 如果执行文件不是需求页可执行文件(ZMAGIC), 或者代码和数据重定位部分长度不等于 0, 
	// 或者(代码段 + 数据段 + 堆)长度超过 50MB, 或者执行文件长度小于(代码段 + 数据段 + 符号表长度 + 执行头部分)长度的总和.
	brelse(bh);
	if (N_MAGIC(ex) != ZMAGIC || ex.a_trsize || ex.a_drsize ||
			ex.a_text + ex.a_data + ex.a_bss > 0x3000000 ||
			inode->i_size < ex.a_text + ex.a_data + ex.a_syms + N_TXTOFF(ex)) {
		retval = -ENOEXEC;
		goto exec_error2;
	}
	// 另外, 如果执行文件中代码开始处没有位于 1 个页面(1024 字节)边界处, 则也不能执行. 
	// 因为需求页(Demand paging)技术要求加载执行文件内容时以页面为单位, 因此要求执行文件映像中代码和数据都从页面边界处开始.
	if (N_TXTOFF(ex) != BLOCK_SIZE) {
		printk("%s: N_TXTOFF != BLOCK_SIZE. See a.out.h.", filename);
		retval = -ENOEXEC;
		goto exec_error2;
	}
	// 如果 sh_bang 标志没有设置, 则复制指定个数的命令行参数和环境字符串到参数和环境空间中. 
	// 若 sh_bang 标志已经设置, 则表明将运行脚本解释程序, 此时环境变量页面已经复制完成, 无需再复制. 
	// 同样, 若 sh_bang 没有置位而需要复制的话, 那么此时指针 p 随着复制信息增加而逐渐向小地址方向移动,
	// 因此这两个复制串函数执行完后, 环境参数串信息块位于程序参数串信息块的后面, 并且 p 指向程序的第 1 个参数. 
	// 事实上, p 是在 128KB 参数和环境空间中的偏移值. 因此如果 p = 0, 则表示环境变量与参数空间页面已经被占满, 容纳不下了.
	// page 中的空间分布  开始-->|             | argv (page 低地址空间处) | envp (page 高地址空间处) |<--page 空间末端
	if (!sh_bang) { 								// sh_bang: 是否执行脚本程序的标志, 0 - 否.
		p = copy_strings(envc, envp, page, p, 0);
		p = copy_strings(argc, argv, page, p, 0);
		if (!p) {
			retval = -ENOMEM;
			goto exec_error2;
		}
	}
	/* OK, This is the point of no return */
	/* note that current->library stays unchanged by an exec */
	/* OK, 下面开始就没有返回的地方了 */
	// 前面我们针对函数参数提供的信息对需要运行执行文件的命令行和环境空间进行了设置, 
	// 但是还没有为执行文件初始化进程任务结构信息, 建立页表等工作, 由于执行文件直接使用当前进程的 "躯壳", 
	// 即当前进程将被改造成执行文件的进程, 因此我们需要首先释放当前进程占用的某些系统资源, 
	// 包括关闭指定的已打开文件, 占用的页表和内存页面等. 
	// 然后根据执行文件头结构信息修改当前进程使用的局部描述符表 LDT 中描述符的内容, 
	// 重新设置代码段和数据段描述符的限长, 
	// 再利用当前处理得到的 e_uid 和 e_gid 等信息来设置进程任务结构中相关的字段. 
	// 最后把执行本次系统调用程序的返回地址 eip[] 指向执行文件中代码的起始位置处. 
	// 这样本系统调用退出返回后就会去运行新执行文件的代码了. 
	// 注意, 虽然此时新执行文件代码和数据还没有从文件中加载到内存中, 
	// 但其参数和环境已经在 copy_strings() 中使用 get_free_page() 分配了物理内存页来保存数据, 
	// 并在 chang_ldt() 函数中使用 put_page() 放到了进程逻辑空间的末端处. 
	// 另外, 在 create_tables() 中也会由于在用户栈上存放参数和环境指针表而引起缺页异常, 
	// 从而内存管理程序也会就此为用户栈空间映射物理内存页.
	//
	// 这里我们首先放回进程原执行程序的 i 节点, 并且让进程 executable 字段指向新执行文件的 i 节点. 
	// 然后复位原进程的所有信号处理句柄, 但对于 SIG_IGN 句柄无须复位.
	if (current->executable)
		iput(current->executable);
	current->executable = inode; 				// 设置当前进程对应的可执行文件的 inode 信息.
	current->signal = 0; 						// 对信号和信号处理函数进行初始化.
	for (i = 0; i < 32; i++) {
		current->sigaction[i].sa_mask = 0;
		current->sigaction[i].sa_flags = 0;
		if (current->sigaction[i].sa_handler != SIG_IGN)
			current->sigaction[i].sa_handler = NULL;
	}
	// 再根据设定的执行时关闭文件句柄(close_on_exec)位图标志, 关闭指定的打开文件并复位该标志.
	for (i = 0; i < NR_OPEN; i++)
		if ((current->close_on_exec >> i) & 1)
			sys_close(i);
	current->close_on_exec = 0;
	// ** 然后根据当前进程指定的基地址和限长, 释放原程序的代码段和数据段所对应的内存页表指定的物理内存页面及页表本身. 
	// 释放完后新执行文件并没有占用 0-640KB 对应的物理页面, 因此在处理器真正运行新执行文件代码时(访问 0x0)就会引起缺页异常中断, 
	// 此时内存管理程序即会执行缺页处理页为新执行文件申请内存页面和设置相关页表项, 并且把相关执行文件页面读入内存中. **
	free_page_tables(get_base(current->ldt[1]), get_limit(0x0f)); 		// 只释放原程序代码/数据段大小的页面. 
	free_page_tables(get_base(current->ldt[2]), get_limit(0x17));		// (初次 execve 时一般是 640KB, 即复制的内核代码/数据段)
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	// 如果 "上次任务使用了协处理器" 指向的是当前进程, 则将其置空, 并复位使用了协处理器的标志.
	current->used_math = 0;
	// 然后我们根据要执行文件的头结构中的代码长度字段 a_text 的值修改局部表中描述符基址和段限长, 
	// 并将 128KB 的参数和环境空间页面放置在数据段 64MB - 4MB - 128KB 处.
	// 执行下面语句之后, p 此时更改成以数据段起始处为原点的偏移值, 但仍指向参数和环境空间数据开始处, 
	// 即已转换成栈指针值. 然后调用内部函数 create_tables() 在栈空间中创建环境和参数变量指针表, 
	// 供程序的 main() 函数作为参数使用, 并返回该栈指针.
	p += change_ldt(ex.a_text, page); 					// ex.a_text 表示文件的代码长度. 此时 p = 64MB + 参数环境空间的偏移值(比如 31KB).
	// | ----------- 约 60MB ---------- | - 128K(参数和环境空间) - | --------- 4MB 动态加载库空间 ---------- |
	p -= LIBRARY_SIZE + (MAX_ARG_PAGES * PAGE_SIZE); 	// 此时 p 指向进程空间的 64MB - 4MB - 128KB + p, 即 p 指向 64MB 进程空间中的参数和环境空间.
	// 经过以下操作后 | -- 参数和环境空间剩余部分 -- (p->) | argc | 命令行参数指针的指针 | 环境变量指针的指针 | 存放命令行参数指针(s) | NULL | 存放环境变量指针(s) | NULL | arg | env | <- 参数和环境空间末端
	p = (unsigned long) create_tables((char *)p, argc, envc); 	// 此时 p 指向栈顶.
	// 接着再修改进程各字段值为新执行文件的信息. 
	// 即令进程任务结构代码尾字段 end_code 等于执行文件的代码段长度 a_text; 
	// 数据尾字段 end_data 等于执行文件的代码段长度加数据段长度(a_data + a_text); 
	// 并令进程堆起始指针字段 brk = a_text + a_data + a_bss. 
	// 进程地址空间分布: | text - data - bss | <- brk ------------------- sp -> | --- (参数和环境空间已占用的空间) --- | --------- 4MB --------- |
	// (**堆由低地址向高地址增长[由 brk 开始增长], 栈由高地址向低地址增长[由 sp 开始增长]**).
	// brk 用于指明进程当前数据段(包括未初始化数据部分)末端位置, 供内核为进程分配内存时指定分配开始位置. 
	// 然后设置进程栈开始字段为栈指针所在页面, 并重新设置进程的有效用户 id 和有效组 id.
	current->brk = ex.a_bss + (current->end_data = ex.a_data + (current->end_code = ex.a_text));
	current->start_stack = p & 0xfffff000; 			// 4KB 边界.
	current->suid = current->euid = e_uid;
	current->sgid = current->egid = e_gid;
	// 最后将原调用系统中断的程序在堆栈上的代码指针 eip[0](保存的是中断返回时的下一行代码)替换为指向新执行程序的入口点(0x0), 
	// 并将栈指针替换为新执行文件的栈指针. 此后返回指令将弹出这些栈数据并使得 CPU 去执行新执行文件, 
	// 因此不会返回到原调用系统中断的程序中去了.
	eip[0] = ex.a_entry;							/* eip, magic happens :-) */	/* eip, 魔法起作用了 */
	eip[3] = p;										/* stack pointer */				/* esp, 堆栈指针 */
	return 0;
exec_error2:
	iput(inode);									// 放回 i 节点.
exec_error1:
	for (i = 0; i < MAX_ARG_PAGES; i++)
		free_page(page[i]);							// 释放存放参数和环境串的内存页面.
	return(retval);									// 返回出错码.
}
