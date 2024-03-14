/*
 *  linux/mm/swap.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This file should contain most things doing the swapping from/to disk.
 * Started 18.12.91
 */
/*
 * 本程序应该包括绝大部分执行内存交换的代码(从内存到磁盘或反之).
 * 从 91 年 12 月 18 日开始编制.
 */

#include <string.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>

// 每个字节 8 位, 因此 1 页(4096B)共有 32768 个位. 
// 若 1 个位对应 1 页内存, 则最多可管理 32768 个页面, 对应 128MB 内存容量.
#define SWAP_BITS (4096 << 3)

// 位操作宏. 通过给定不同的 "op", 可定义对指定比特位进行测试, 设置或清除三种操作.
// 参数 addr 是指定线性地址; nr 是指定地址处开始的比特位偏移位. 
// 该宏把给定地址 addr 处第 nr 个比特位的值放入进位标志, 设置或复位该比特位并返回进位标志值(即原位值).
// 第 25 行上第一个指令随 "op" 字符的不同而组合形成不同的指令:
// 当 op = "" 时, 就是指令 bt - (Bit Test)测试并用原值设置进位位.
// 当 op = "s" 时, 就是指令 bts - (Bit Test and Set)设置比特位值并用原来设置进位位.
// 当 op = "r" 时, 就是指令 btr - (Bit Test and Reset)复位比特位值并原值设置进位位.
// 输入: %0 - (返回值); %1 - 位偏移(nr); %2 - 基址(addr); %3 - 加操作寄存器初值(0).
// 内嵌汇编代码把基地址(%2)和比特偏移值(%1)所指定的比特位值先保存到进位标志 CF 中, 然后设置(复位)该比特位. 
// 指令 abcl 是带进位位加, 用于根据进位位 CF 设置操作数(%0). 如果 CF = 1 则返回寄存器值 = 1, 否则返回寄存器值 = 0.
#define bitop(name, op) \
static inline int name(char * addr, unsigned int nr) \
{ \
int __res; \
__asm__ __volatile__("bt" op " %1, %2; adcl $0, %0" \
:"=g" (__res) \
:"r" (nr),"m" (*(addr)),"0" (0)); \
return __res; \
}

// 这里根据不同的 op 字符定义 3 个内嵌函数.
bitop(bit, "")								// 定义内嵌函数 bit(char * addr, unsigned int nr).
bitop(setbit, "s")							// 定义内嵌函数 setbit(char * addr, unsigned int nr).
bitop(clrbit, "r")							// 定义内嵌函数 clrbit(char * addr, unsigned int nr).

static char * swap_bitmap = NULL;           // 交换内存的位图所在物理页面的指针.
int SWAP_DEV = 0;							// 内核初始化时设置的交换设备号.

/*
 * We never page the pages in task[0] - kernel memory.
 * We page all other pages.
 */
/*
 * 我们不会交换任务 0(task[0]) 的页面, 即不交换内核页面, 我们只对其他页面进行交换操作.
 */
// 第 1 个虚拟内存页面, 即从任务 0 末端(64MB)处开始的虚拟内存页面.
#define FIRST_VM_PAGE (TASK_SIZE >> 12)			// = 64MB/4KB = 16384
#define LAST_VM_PAGE (1024 * 1024)				// = 4GB/4KB = 1048576 4G 对应的页数
#define VM_PAGES (LAST_VM_PAGE - FIRST_VM_PAGE)	// = 1032192(从 0 开始计)(用总的页面数减去第 0 个任务的页面数)

// 申请 1 页交换页面.
// 扫描整个交换映射位图(除对应位图本身的位 0 以外), 返回值为 1 的第一个比特位号, 即目前空闲的交换页面号. 
// 若操作成功则返回交换页面号, 否则返回 0.
static int get_swap_page(void)
{
	int nr;

	if (!swap_bitmap)
		return 0;
	for (nr = 1; nr < 32768 ; nr++)
		if (clrbit(swap_bitmap, nr))
			return nr;					// 返回目前空闲的交换页面号.
	return 0;
}

// 释放交换设备中指定的交换页面.
// 在交换位图中设置指定页面号对应的位(置 1). 
// 若原来该位就等于 1, 则表示交换设备中原来该页面就没有被占用, 或者位图出错, 于是显示出错信息并返回.
void swap_free(int swap_nr)				// 参数指定交换页面号.
{
	if (!swap_nr)
		return;
	if (swap_bitmap && swap_nr < SWAP_BITS)
		if (!setbit(swap_bitmap, swap_nr))
			return;
	printk("Swap-space bad (swap_free())\n\r");
	return;
}

// 把指定页面交换进内存中
// 把指定页表项的对应页面从交换设备中读入到新申请的内存页面中. 
// 修改交换位图中对应位(置位), 同时修改页表项内容, 让它指向该内存页面, 并设置相应标志.
void swap_in(unsigned long *table_ptr)
{
	int swap_nr;
	unsigned long page;

	// 首先检查交换位图和参数有效性. 
	// 如果交换位图不存在, 或者指定页表项对应的页面已存在于内存中, 或者交换页面号为 0, 则显示警告信息并退出. 
	// 对于已放到交换设备中去的内存页面, 相应页表项中存放的应是交换页面号 * 2, 即(swap_nr << 1).
	if (!swap_bitmap) {
		printk("Trying to swap in without swap bit-map");
		return;
	}
	if (1 & *table_ptr) {
		printk("trying to swap in present page\n\r");
		return;
	}
	swap_nr = *table_ptr >> 1;
	if (!swap_nr) {
		printk("No swap page in swap_in\n\r");
		return;
	}
	// 然后申请一页物理内存并从交换设备中读入页面号为 swap_nr 的页面. 
	// 在把页面交换进来后, 就把交换位图中对应比特位置位. 
	// 如果其原本就是置位的, 说明此次是再次从交换设备中读入相同的页面, 于是显示一下警告信息. 
	// 最后让页表指向该物理页面, 并设置页面已修改, 用户可读写和存在标志(Dirty, U/S, R/W, P).
	if (!(page = get_free_page()))
		oom();
	read_swap_page(swap_nr, (char *) page);
	if (setbit(swap_bitmap, swap_nr))
		printk("swapping in multiply from same page\n\r");
	*table_ptr = page | (PAGE_DIRTY | 7);
}

// 尝试把页面交换出去.
// 若页面没有被修改过则不必保存在交换设备中, 因为对应页面还可以再直接从相应映像文件中读入. 
// 于是可以直接释放掉相应物理页面了事. 否则就申请一个交换页面号, 然后把页面交换出去. 
// 此时交换页面号要保存在对应页表项中, 并且仍需要保持页表项存在位 P = 0. 
// 参数是页表项指针. 页面换或释放成功返回 1, 否则返回 0.
int try_to_swap_out(unsigned long * table_ptr)
{
	unsigned long page;
	unsigned long swap_nr;

	// 首先判断参数的有效性. 若需要交换出去的内存页面并不存在(或称无效), 则即可退出. 
	// 若页表项指定的物理页面地址大于分页管理的内存高端 PAGING_MEMORY(15MB), 也退出.
	page = *table_ptr;
	if (!(PAGE_PRESENT & page))
		return 0;
	if (page - LOW_MEM > PAGING_MEMORY)
		return 0;
	// 若内存页面已被修改过, 但是该页面是被共享的, 那么为了提高运行效率, 此类页面不宜被交换出去, 于是直接退出, 函数返回 0. 
	// 否则就申请一交换页面号, 并把它保存在页表项中, 然后把页面交换出去并释放对应物理内存页面.
	if (PAGE_DIRTY & page) {
		page &= 0xfffff000;									// 取物理页面地址.
		if (mem_map[MAP_NR(page)] != 1)
			return 0;
		if (!(swap_nr = get_swap_page()))					// 申请交换页面号.
			return 0;
		// 对于要交换设备中的页面, 相应页表项中将存放的是(swap_nr << 1). 
		// 乘 2(左移 1 位) 是为了空出原来页表项的存在位(P). 
		// 只有存在位 P = 0 并且页表项内容不为 0 的页面才会在交换设备中. 
		// Intel 手册中明确指出, 当一个表项的存在位 P = 0 时(无效页表项), 
		// 所有其他位(位 31-1)可供随意使用. 
		// 下面写交换页函数 write_swap_page(nr, buffer) 被定义为 ll_rw_page(WRITE, SWAP_DEV, (nr), (buffer)).
		*table_ptr = swap_nr << 1;
		invalidate();										// 刷新 CPU 页变换高速缓冲.
		write_swap_page(swap_nr, (char *) page);
		free_page(page);
		return 1;
	}
	// 否则表明页面没有修改过. 那么就不用交换出去, 而直接释放即可.
	*table_ptr = 0;
	invalidate();
	free_page(page);
	return 1;
}

/*
 * Ok, this has a rather intricate logic - the idea is to make good
 * and fast machine code. If we didn't worry about that, things would
 * be easier.
 */
/*
 * OK, 这个函数中有一个非常复杂的逻辑, 用于产生逻辑性好并且速度快的机器码. 如果我们不对此操心的话, 那么事情可能更容易些.
 */
// 把内存页面放到交换设备中.
// 从线性地址 64MB 对应的目录项(FIRST_VM_PAGE >> 10)开始, 搜索整个 4GB 线性空间, 
// 对有效页目录二级页表指定的物理内存页面执行交换到交换设备中去的尝试. 
// 一旦成功地交换出一个页面, 就返回 -1. 否则返回 0. 该函数会在 get_free_page() 中被调用.
int swap_out(void)
{
	static int dir_entry = FIRST_VM_PAGE >> 10;	// 即任务 1 的第 1 个目录项索引.
	static int page_entry = -1;
	int counter = VM_PAGES;						// 表示除去任务 0 以外的其他任务的所有页数目
	int pg_table;

	// 首先搜索页目录表, 查找二级页表存在的页目录项 pg_table. 
	// 找到则退出循环, 否则高速页目录项数对应剩余二级页表项数 counter, 
	// 然后继续检测下一项目录项. 若全部搜索完还没有找到适合的(存在的)页目录项, 就重新搜索.
	while (counter > 0) {
		pg_table = pg_dir[dir_entry];			// 页目录项内容.
		if (pg_table & 1)
			break;
		counter -= 1024;						// 1 个页表对应 1024 个页帧
		dir_entry++;							// 下一目录项.
		// 如果整个 4GB 的 1024 个页目录项检查完了则又回到第 1 个任务重新开始检查
		if (dir_entry >= 1024)
			dir_entry = FIRST_VM_PAGE >> 10;
	}
	// 在取得当前目录项的页表指针后, 针对该页表中的所有 1024 个页面, 逐一调用交换函数 try_to_swap_out() 尝试交换出去. 
	// 一旦某个页面成功交换到交换设备中就返回 1. 
	// 若对所有目录项的所有页表都已尝试失败, 则显示 "交换内存用完" 的警告, 并返回 0.
	pg_table &= 0xfffff000;						// 页表指针(地址)(页对齐)
	while (counter-- > 0) {
		page_entry++;
		// 如果已经尝试处理完当前页表所有项还没有能够成功地交换出一个页面, 即此时页表项索引大于等于 1024, 
		// 则如同前面第 135-143 行执行相同的处理来选出一个二级页表存在的页目录项, 并取得相应二级页表指针.
		if (page_entry >= 1024) {
			page_entry = 0;
		repeat:
			dir_entry++;
			if (dir_entry >= 1024)
				dir_entry = FIRST_VM_PAGE >> 10;
			pg_table = pg_dir[dir_entry];		// 页目录项内容.
			if (!(pg_table & 1))
				if ((counter -= 1024) > 0)
					goto repeat;
				else
					break;
			pg_table &= 0xfffff000;				// 页表指针.
		}
		if (try_to_swap_out(page_entry + (unsigned long *) pg_table))
			return 1;
        }
	printk("Out of swap-memory\n\r");
	return 0;
}

/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 */
/*
 * 获取(mem_map 中的)首个(实际上是最后 1 个)空闲页面, 并标志为已使用. 如果没有空闲页面, 就返回 0.
 */
// 在主内存区(主内存区在高速缓冲区之后)中申请 1 页空闲物理页面(主内存区每个页面是 4KB, 高速缓冲区每个页面是 1KB).
// 如果已经没有可用物理页面, 则调用执行交换处理. 然后再次申请页面.
// 输入: %1(ax = 0) - 0; %2(LOW_MEM) 内存字节位图管理的起始位置; 
// 		%3(cx = PAGING_PAGES); %4(edi = mem_map + PAGING_PAGES - 1).
// 输出: 返回 %0(ax = 物理页面起始地址). 
// 函数返回空闲页面的物理地址(页面起始地址).
// 上面 %4 寄存器实际指向 mem_map[] 内存字节位图的最后一个字节. 
// 本函数从位图末端开始向前扫描所有页面标志(页面总数为 PAGING_PAGES), 
// 若有页面空闲(内存位图字节为 0)则返回页面地址. 
// 注意! 本函数只是指出在主内存区的一页空闲物理页面, 但并没有映射到某个进程的地址空间中去. 
// 后面的 put_page() 函数即用于把指定页面映射到某个进程的地址空间中. 
// 当然对于内核使用本函数并不需要再使用 put_page() 进行映射, 
// 因为内核代码和数据空间(16MB)已经对等地映射到物理地址空间.
unsigned long get_free_page(void)
{
register unsigned long __res;

// 首先在内存映射字节位图(mem_map[]: 位于内核数据段中, 由编译时生成位置)中查找值为 0(UNUSED) 的项, 
// 然后把对应物理内存页面清零. 如果得到的页面地址大于实际物理内存容量则重新寻找. (mem_map 位于 mm/memory.c)
// 如果没有找到空闲页面则去调用执行交换处理, 并重新查找. 最后返回空闲物理页面地址.
// repne; scasb 用于搜索 edi 指向的内存(mem_map[PAGEING_PAGES-1])[即 mem_map 中最后一项开始]中与 eax 中值(0)相同的项.
repeat:
	__asm__(
		"std ; repne ; scasb			/* 置方向位, al(0) 与对应每个页面的(di)内容比较, 每循环一次 ecx 都会递减, 最终 ecx 等于 mem_map 中为 0 的最高项的下标(3807) */\n\t"
		"jne 1f							/* 如果没有等于 0 的字节, 则跳转结束(返回 0). */\n\t"
		"movb $1, 1(%%edi)				/* edi 指向对应的 mem_map 数组项的内存地址, [edi + 1] = mem_map[i] = 1, 将对应页面内存映像字节(mem_map[i] 的值)置 1. */\n\t"
		"sall $12, %%ecx				/* ecx 左移 12 位: 空闲页面号(3807) * 4K = 空闲页面起始地址. */\n\t"
		"addl %2, %%ecx					/* 再加上低端内存地址(LOW_MEM = 1MB, 参考 PAGING_MEMORY 就可以知道为什么 +1MB), 得到空闲页面实际物理起始地址. */\n\t"
		"movl %%ecx, %%edx				/* 将空闲页面实际起始地址放到 edx 寄存器. */\n\t"
		"movl $1024, %%ecx				/* 寄存器 ecx 置计数值 1024. */\n\t"
		"leal 4092(%%edx), %%edi		/* 将 4092 + edx 的地址放入 edi 寄存器(该页面的末端). */\n\t"
		"rep ; stosl					/* 将 edi 所指内存清零(反方向, 即将该页面清零). */\n\t"
		"movl %%edx, %%eax				/* 将空闲页面起始地址 -> eax(返回值). */\n\t"
		"1:\n\t"
		"cld"
		:"=a" (__res) 									/* =a 表示这是输出寄存器 %0(eax) */
		:"0" (0), "i" (LOW_MEM), "c" (PAGING_PAGES), 	/* 输入寄存器列表: %1(=%0=eax), %2, %3(ecx); LOW_MEM = 1MB; PAGING_PAGES = 3840 */
		"D" (mem_map + PAGING_PAGES - 1) 				/* %4(edi) */
		:"dx");
	if (__res >= HIGH_MEMORY)				// 页面地址大于实际内存容量则重新寻找
		goto repeat;
	if (!__res && swap_out())				// 若没有得到空闲页面则执行交换处理, 并重新查找.
		goto repeat;
	return __res;							// 返回空闲物理页面地址.
}

// 交换内存初始化.
void init_swapping(void)
{
	// blk_size[] 指向指定主设备号的块设备数据块总数数组. 
	// 该块数数组每一项对应一个设备上所拥有的数据块总数(1 块大小 = 1KB).
	extern int *blk_size[];					// 存放各个块设备大小的指针数组. (kernel/blk_drv/ll_rw_blk.c)
	int swap_size, i, j;

	// 如果没有定义交换设备则返回. 如果交换设备没有设置块数数组, 则显示并返回.
	if (!SWAP_DEV)
		return;
	if (!blk_size[MAJOR(SWAP_DEV)]) {  				// SWAP_DEV = 0x304: 硬盘的第 4 个分区.
		printk("Unable to get size of swap device\n\r");
		return;
	}
	// 取指定交换设备号的交换区数据块总数 swap_size. 
	// 若为 0 则返回, 若总块数小于 100 块(100KB)则显示信息 "交换设备区太小", 然后退出.
	swap_size = blk_size[MAJOR(SWAP_DEV)][MINOR(SWAP_DEV)];
	if (!swap_size)
		return;
	if (swap_size < 100) {
		printk("Swap device too small (%d blocks)\n\r", swap_size);
		return;
	}
	// 每页 4 个数据块(4096KB), 所以 swap_size >>= 2 计算出交换页面总数.
	// 交换数据块总数转换成对应可交换页面总数. 该值不能大于 SWAP_BITS 所能表示的页面数. 即交换页面总数不得大于 32768.
	swap_size >>= 2; 								// swap_size /= 4.
	if (swap_size > SWAP_BITS)
		swap_size = SWAP_BITS;
	// 然后从主内存区申请一页物理内存(4KB)来存放交换页面映射数组 swap_bitmap, 其中每 1 比特代表 1 页交换页面.
	swap_bitmap = (char *) get_free_page();
	if (!swap_bitmap) {
		printk("Unable to start swapping: out of memory :-)\n\r");
		return;
	}
	// read_swap_page(nr, buffer) 被定义为 ll_rw_page(READ, SWAP_DEV, (nr), (buffer)). 
	// 这里把交换设备上的页面 0 读到 swap_bitmap 页面中, 该页面是交换区管理页面. 
	// **其中第 4086 字节开始处含有 10 个字符的交换设备特征字符串 "SWAP-SPACE". 
	// 若没有找到该特征字符串, 则说明不是一个有效的交换设备.**
	// 于是显示信息, 释放刚申请的物理页面并退出函数. 否则将特征字符串字节清零.
	read_swap_page(0, swap_bitmap);
	if (strncmp("SWAP-SPACE", swap_bitmap + 4086, 10)) {
		printk("Unable to find swap-space signature\n\r");
		free_page((long) swap_bitmap);
		swap_bitmap = NULL;
		return;
	}
	// 将交换设备的标志字符串 "SWAP-SPACE" 字符串清空.
	memset(swap_bitmap + 4086, 0, 10); 					// 用字符 'x' 填写指定长度的内存.
	// 然后检查读入的交换位映射图. 其中共有 32768 个比特位. 
	// 若位图中的比特位为 0, 则表示设备上对应的交换页面已被使用, 若比特位为 1, 则表示对应的交换页面可用.
	// 因此对于设备上的交换分区, 第一个页面(页面 0)已被用作交换管理页面, 位为 0.
	// 而页面 [1 -- swap_size-1] 是可用的, 因此它们对应在位图中的比特位应该均为 1(空闲).
	// 位图中 [swap_size -- SWAP_BITS] 范围内的比特位由于没有交换分区中对应的交换页面, 所以它们应该被初始化为 0(占用).
	// 先检查不可用的位图情况, 它们应该均为 0, 若这部分(0, swap_size--SWAP_BITS)位图中有置位的比特位(1), 
	// 则表示位图有问题, 于是显示出错信息, 释放位图占用的页面并退出函数. 
	for (i = 0; i < SWAP_BITS; i++) {
		if (i == 1)
			i = swap_size; 							// 跳到 swap_size 之后的位继续检查.
		if (bit(swap_bitmap, i)) {
			printk("Bad swap-space bit-map\n\r");
			free_page((long) swap_bitmap);
			swap_bitmap = NULL;
			return;
		}
	}
	// 然后再检查和统计[位 1 到位 swap_size]之间的所有比特位是否为 1(空闲). 
	// 若统计出没有空闲的交换页面, 则表示交换功能有问题, 于是释放位图占用的内存页面并退出函数.
	// 否则显示交换设备工作正常以及交换页面和交换空间总字节数.
	j = 0;
	for (i = 1; i < swap_size; i++)
		if (bit(swap_bitmap, i))
			j++;
	if (!j) { 									// 如果没有空闲的交换页面, 则表示交换设备有问题.
		free_page((long) swap_bitmap);
		swap_bitmap = NULL;
		return;
	}
	Log(LOG_INFO_TYPE, "<<<<< Swap device ok: %d pages(%d bytes) swap-space. >>>>>\n\r", j, j * 4096);
}
