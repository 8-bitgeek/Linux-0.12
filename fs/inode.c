/*
 *  linux/fs/inode.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/kernel.h>
// #include <linux/mm.h>
#include <asm/system.h>

// 设置数据块总数指针数组. 每个指针项指向指定主设备号的总块数数组 hd_sizes[]. 
// 该总块数数组每一项对应子设备号确定的一个子设备上所拥有的数据块总数(1 块大小 = 1KB).
extern int *blk_size[];

struct m_inode inode_table[NR_INODE]={{0, }, };   					// 内存中 i 节点表(NR_INODE = 64 项)

static void read_inode(struct m_inode * inode);						// 读指定 i 节点号的 i 节点信息.
static void write_inode(struct m_inode * inode);					// 写 i 节点信息到高速缓冲中.

// 等待指定的 i 节点可用(解锁).
// 如果 i 节点已被锁定, 则将当前任务置为不可中断的等待状态, 并添加到该 i 节点的等待队列 i_wait 中. 
// 直到该 i 节点解锁并明确地唤醒本任务.
static inline void wait_on_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);									// kernel/sched.c
	sti();
}

// 对 i 节点上锁(锁定指定的 i 节点)
// 如果 i 节点已被锁定, 则将当前任务置为不可中断的等待状态, 并添加到该 i 节点的等待队列 i_wait 中.
// 直到该 i 节点解锁并明确地唤醒本任务. 然后对其上锁.
static inline void lock_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	inode->i_lock = 1;												// 置锁定标志.
	sti();
}

// 对指定的 i 节点解锁.
// 复位 i 节点的锁定标志, 并明确地唤醒等待在此 i 节点等待队列 i_wait 上的所有进程.
static inline void unlock_inode(struct m_inode * inode)
{
	inode->i_lock = 0;
	wake_up(&inode->i_wait);										// kernel/sched.c
}

// 释放设备 dev 在内存 i 节点表中的所有 i 节点. 
// 扫描内存中的 i 节点表数组, 如果是指定设备使用的 i 节点就释放之. 
void invalidate_inodes(int dev)
{
	int i;
	struct m_inode * inode;

	// 首先让指针指向内存 i 节点表数组首项. 然后扫描 i 节点表指针数组中的所有 i 节点. 
	// 针对其中每个 i 节点, 先等待该 i 节点解锁可用(若目前正被上锁的话), 再判断是否属于指定设备的 i 节点. 
	// 如果是指定设备的 i 节点, 则看看它是否还被使用着, 即其引用计数是否不为 0. 若是则显示警告信息. 
	// 然后释放之, 即把 i 节点的设备号字段 i_dev 置. 
	// 第 50 行上的指针赋值 "0 + inode_table" 等同于 "inode_table", "&inode_table[0]". 
	// 不过这样写可能更明了一些. 
	inode = 0 + inode_table;                  						// 指向 i 节点表指针数组首项. 
	for(i = 0 ; i < NR_INODE ; i++, inode++) {
		wait_on_inode(inode);           							// 等待该 i 节点可用(解锁). 
		if (inode->i_dev == dev) {
			if (inode->i_count)     								// 若其引用数不为 0, 则显示出错警告. 
				printk("inode in use on removed disk\n\r");
			inode->i_dev = inode->i_dirt = 0;       				// 释放 i 节点(置设备号为 0). 
		}
	}
}

// 同步所有 i 节点. 
// 把内存 i 节点表中所有 i 节点与设备上 i 节点作同步操作. 
void sync_inodes(void)
{
	int i;
	struct m_inode * inode;

	// 首先让内存 i 节点类型的指针指向 i 节点表首项, 然后扫描整个 i 节点表中的节点. 
	// 针对其中每个 i 节点, 先等待该 i 节点解锁可用(若目前正被上锁的话), 然后判断该 i 节点是否已被修改并且不是管道节点. 
	// 若是这种情况则将该 i 节点写入高速缓冲区中, 缓冲区管理程序 buffer.c 会在适当时机将它们写入盘中. 
	inode = 0 + inode_table;                          				// 让指针首先指向 i 节点表指针数组首项. 
	for(i = 0 ; i < NR_INODE ; i++, inode++) {           			// 扫描 i 节点表指针数组. 
		wait_on_inode(inode);                   					// 等待该 i 节点可用(解锁). 
		if (inode->i_dirt && !inode->i_pipe)    					// 若 i 节点已修改且不是管道节点, 
			write_inode(inode);             						// 则写盘(实际是写入缓冲区中). 
	}
}

// 文件数据块映射到盘块的处理操作. (block 位图处理函数, bmap - block map)
// 参数: inode - 文件的 i 节点指针; block - 文件中的数据块号; create - 创建块标志. 
// 该函数把指定的文件数据块 block 对应到设备上逻辑块上, 并返回逻辑块号.
// 如果创建标志置位, 则在设备上对应逻辑块不存在时就申请新磁盘块, 返回文件数据块 block 对应在设备上的逻辑块号(盘块号).
static int _bmap(struct m_inode * inode, int block, int create)
{
	struct buffer_head * bh;
	int i;

	// 首先判断参数文件数据块号 block 的有效性. 如果块号小于 0, 则停机. 
	// 如果块号大于直接块数 + 间接块数 + 二次间接块数, 超出文件系统表示范围, 则停机.
	if (block < 0)
		panic("_bmap: block < 0");
	if (block >= 7 + 512 + 512 * 512)
		panic("_bmap: block > big");
	// 然后根据文件块号的大小值和是否设置了创建标志分别进行处理. 如果该块号小于 7, 则使用直接块表示. 
	// 如果创建标志置位, 并且 i 节点中对应该块的逻辑块(区段)字段为 0, 则向相应设备申请一磁盘块(逻辑块),
	// 并且将盘上逻辑块号(盘块号)填入逻辑块字段中. 然后设置 i 节点改变时间, 置 i 节点已修改标志. 
	// 最后返回逻辑块号. 函数 new_block() 定义在 bitmap.c 程序中.
	if (block < 7) {
		if (create && !inode->i_zone[block])
			if (inode->i_zone[block] = new_block(inode->i_dev)) {
				inode->i_ctime = CURRENT_TIME;
				inode->i_dirt = 1;
			}
		return inode->i_zone[block];
	}
	// 如果该块号 >= 7, 且小于 7 + 512, 则说明使用的是一次间接块. 下面对一次间接块进行处理. 
	// 如果是创建, 并且该 i 节点中对应间接块字段 i_zone[7] 是 0, 表明文件是首次使用间接块,
	// 则需申请一磁盘块用于存放间接块信息, 并将此实际磁盘块号填入间接块字段中. 然后设置 i 节点已修改标志和修改时间. 
	// 如果创建时申请磁盘块失败, 则此时 i 节点间接块字段 i_zone[7] 为 0, 则返回 0. 
	// 或者不是创建, 但 i_zone[7] 原来就为 0, 表明 i 节点中没有间接块, 于是映射磁盘块失败, 返回 0 退出.
	block -= 7;
	if (block < 512) {
		// 如果创建标志置位, 同时索引 7 这个位置没有绑定到对应的逻辑块, 则申请一个逻辑块
		if (create && !inode->i_zone[7])
			if (inode->i_zone[7] = new_block(inode->i_dev)) {
				inode->i_dirt = 1;
				inode->i_ctime = CURRENT_TIME;
			}
		if (!inode->i_zone[7])
			return 0;
		// 现在读取设备上该 i 节点的一次间接块. 并取该间接块上第 block 项中的逻辑块号(盘块号)i. 
		// 每一项占 2 个字节. 如果是创建并且间接块的第 block 项中的逻辑块号为 0 的话, 则申请一磁盘块,
		// 并让间接块中的第 block 项等于该新逻辑块块号, 然后置位间接块的已修改标志. 
		// 如果不是创建, 则 i 就是需要映射(寻找)的逻辑块号.
		if (!(bh = bread(inode->i_dev, inode->i_zone[7])))
			return 0;
		i = ((unsigned short *) (bh->b_data))[block];
		if (create && !i)
			if (i = new_block(inode->i_dev)) {
				((unsigned short *) (bh->b_data))[block] = i;
				bh->b_dirt = 1;
			}
		// 最后释放该间接块占用的缓冲块, 并返回磁盘上新申请或原有的对应 block 的逻辑块块号.
		brelse(bh);
		return i;
	}
	// 若程序运行到此, 则表明数据块属于二次间接块. 其处理过程与一次间接块类似. 
	// 下面是对二次间接块的处理. 首先将 block 再减去间接块所容纳的块数(512). 
	// 然后根据是否设置了创建标志进行创建或寻找处理. 
	// 如果是新创建并且 i 节点的二次间接块字段为 0, 则需申请一磁盘块用于存放二次间接块的一级块信息, 
	// 并将此实际磁盘块号填入二次间接块字段中. 之后, 置 i 节点已修改编制和修改时间. 
	// 同样地, 如果创建时申请磁盘块失败, 则此时 i 节点二次间接块字段 i_zone[8] 为 0, 则返回 0.
	// 或者不是创建, 但 i_zone[8] 原来变为 0, 表明 i 节点中没有间接块, 于是映射磁盘块失败, 返回 0 退出.
	block -= 512;
	if (create && !inode->i_zone[8])
		if (inode->i_zone[8] = new_block(inode->i_dev)) {
			inode->i_dirt = 1;
			inode->i_ctime = CURRENT_TIME;
		}
	if (!inode->i_zone[8])
		return 0;
	// 现在读取设备上该 i 节点的二次间接块. 并取该二次间接块的一级块上第(block / 512)项中的逻辑块号 i. 
	// 如果是创建并且二次间接块的一级块上第(block / 512)项中的逻辑块号为 0 的话, 
	// 则需申请一磁盘块(逻辑块)作为二次间接块的二级块 i, 并让二次间接块的一级块中第(block / 512)项等于该二级块的块号 i. 
	// 然后置位二次间接块的一级块已修改标志. 并释放二次间接块的一级块. 如果不是创建, 则i就是需要映射(寻找)的逻辑块号.
	if (!(bh = bread(inode->i_dev, inode->i_zone[8])))
		return 0;
	i = ((unsigned short *)bh->b_data)[block >> 9];
	if (create && !i)
		if (i = new_block(inode->i_dev)) {
			((unsigned short *) (bh->b_data))[block >> 9] = i;
			bh->b_dirt=1;
		}
	brelse(bh);
	// 如果二次间接块的二级块块号为 0, 表示申请磁盘失败或者原来对应块号就为 0, 则返回 0 退出.
	// 否则就从设备上读取二次间接块的二级块, 
	// 并取该二级块上第 block 项中的逻辑块号(与上 511 是为了限定 block 值不超过 511).
	if (!i)
		return 0;
	if (!(bh = bread(inode->i_dev, i)))
		return 0;
	i = ((unsigned short *)bh->b_data)[block & 511];
	// 如果是创建并且二级块的第 block 项中逻辑块号为 0 的话, 则申请一磁盘块(逻辑块), 作为最终存放数据信息的块. 
	// 并让二级块中的第 block 项等于该新逻辑块块号(i). 然后置位二级块的已修改标志.
	if (create && !i)
		if (i = new_block(inode->i_dev)) {
			((unsigned short *) (bh->b_data))[block & 511] = i;
			bh->b_dirt = 1;
		}
	// 最后释放该二次间接块的二级块, 返回磁盘上新申请的或原有的对应 block 的逻辑块块号.
	brelse(bh);
	return i;
}

// 取文件数据块 block 在设备上对应的逻辑块号.
// 参数: inode - 文件的内存 i 节点指针; block - 文件中的数据块号.
// 若操作成功则返回对应的逻辑块号, 否则返回 0.
int bmap(struct m_inode * inode, int block)
{
	return _bmap(inode, block, 0);
}

// 取文件数据块block在设备上对应的逻辑块号. 如果对应的逻辑块不存在就创建一块. 并返回设备上对应的逻辑块号. 
// 参数：inode - 文件的内在i节点指针；block - 文件中的数据块号. 
// 若操作成功则返回对应的逻辑块号, 否则返回 0.
int create_block(struct m_inode * inode, int block)
{
	return _bmap(inode, block, 1);
}

// 放回(放置)一个 i 节点(并将 i 节点元数据写入设备).
// 该函数主要用于把 i 节点引用计数值递减 1, 并且若是管道 i 节点, 则唤醒等待的进程. 
// 若是块设备文件 i 节点则刷新设备, 并且若 i 节点的链接计数(i_nlinks)为 0, 
// 则释放该 i 节点占用的所有磁盘逻辑块, 并释放该 i 节点.
void iput(struct m_inode * inode)
{
	// 首先判断参数给出的 i 节点的有效性, 并等待 inode 节点解锁(如果已经上锁的话). 
	// 如果 i 节点的引用计数为 0, 表示该 i 节点已经是空闲的. 
	// 内核再要求对其进行放回操作, 说明内核中其他代码有问题. 于是显示错误信息并停机.
	if (!inode)
		return;
	wait_on_inode(inode);
	if (!inode->i_count)
		panic("iput: trying to free free inode");
	// 如果是管道 i 节点, 则唤醒等待该管道的进程, 引用次数减 1, 如果还有引用则返回. 
	// 否则释放管道占用的内存页面, 并复位该节点的引用计数值, 已修改标志和管道标志, 并返回. 
	// 对于管道节点, inode -> i_size存放着内存页地址. 参见 get_pipe_inode().
	if (inode->i_pipe) {
		wake_up(&inode->i_wait);
		wake_up(&inode->i_wait2);
		if (--inode->i_count)
			return;
		free_page(inode->i_size);
		inode->i_count = 0;
		inode->i_dirt = 0;
		inode->i_pipe = 0;
		return;
	}
	// 如果 i 节点对应的设备号 = 0, 则将此节点的引用计数递减 1, 返回. 例如用于管道操作的 i 节点, 其 i 节点的设备号为 0.
	if (!inode->i_dev) {
		inode->i_count--;
		return;
	}
	// 如果是块设备文件的 i 节点, 此时逻辑块字段 0(i_zone[0]) 中是设备号, 则刷新该设备. 并等待 i 节点解锁.
	if (S_ISBLK(inode->i_mode)) {
		sync_dev(inode->i_zone[0]);
		wait_on_inode(inode);
	}
	// 如果 i 节点引用计数大于 1, 则计数递减 1 后就直接返回(因为该 i 节点还有人在用, 不能释放), 
	// 否则就说明 i 节点的引用计数值为 1(因为上面已经判断过计数是否为零).
	// 如果 i 节点的链接数为 0, 则说明 i 节点对应文件被删除. 于是释放该 i 节点的所有逻辑块, 并释放该 i 节点. 
	// 函数 free_inode() 用于实际释放 i 节点操作, 即复位 i 节点对应的 i 节点位图, 清空 i 节点结构内容.
repeat:
	if (inode->i_count > 1) {
		inode->i_count--;
		return;
	}
	// 当前引用计数为 1.
	if (!inode->i_nlinks) {
		// 释放该 i 节点对应的所有逻辑块
		truncate(inode);
		// 从该设备的超级块中删除该 i 节点
		free_inode(inode);      								// bitmap.c
		return;
	}
	// 如果该 i 节点已作过修改, 则回写更新该 i 节点, 并等待该 i 节点解锁. 
	// 由于这里在写 i 节点时需要等待睡眠, 此时其他进程有可能修改该 i 节点, 
	// 因此在进程被唤醒后需要重复进行上述判断过程(repeat).
	if (inode->i_dirt) {
		write_inode(inode);										/* we can sleep - so do again */
		wait_on_inode(inode);									/* 因为我们睡眠了, 所以要重复判断 */
		goto repeat;
	}
	// 程序若能执行到此, 说明该 i 节点的引用计数值 i_count 是 1, 链接数不为零, 并且内容没有被修改过. 
	// 因此此时只要把 i 节点引用计数递减 1, 返回. 此时该 i 节点的 i_count = 0, 表示已释放.
	inode->i_count--;
	return;
}

// 从 i 节点表(inode_table)中获取一个空闲 i 节点项(脏标志为 0, 且未上锁).
// 寻找空闲的 i 节点项(引用计数 i_count 为 0 的 i 节点), 并将其写盘(在 i_dirt = 1 的情况下写盘, 否则不需要写), 
// 清零该 inode 的信息, 为新的 inode 准备, 引用计数被置 1, 返回其指针. 
struct m_inode * get_empty_inode(void)
{
	struct m_inode * inode;
	static struct m_inode * last_inode = inode_table;			// 指向 i 节点表第 0 项.
	int i;

	// 在 inode_table 表中寻找空闲 i_node 项, 直到找到一个空闲项, 否则一直循环查找.
	do {
		inode = NULL;
		// 在初始化 last_inode 指针指向 i 节点表第一(0)项后循环扫描整个 i 节点表, 
		// 如果 last_inode 已经指向 i 节点表的最后一项之后, 则让其重新指向 i 节点表开始处,
		// 以继续循环寻找空闲 i 节点项. 如果 last_inode 所指向的 i 节点计数值为 0, 则说明可能找到空闲 i 节点项. 
		// 让 inode 指向该 i 节点. 如果该 i 节点的已修改标志和和锁定标志均为 0, 则我们可以使用该 i 节点, 于是退出 for 循环.
		for (i = NR_INODE; i; i--) {							// NR_INODE = 64.
			// TODO: ++last_inode 有导致一个问题: last_inode 会在系统初始化时由 inode_table[1] 开始寻找空闲项.
			if (++last_inode >= inode_table + NR_INODE) 		// 如果超出列表末尾则从头开始.
				last_inode = inode_table;
			if (!last_inode->i_count) { 						// 引用次数 i_count == 0, 即该节点空闲.
				inode = last_inode;
				if (!inode->i_dirt && !inode->i_lock) 			// 脏标志为 0, 且未上锁表示已找到空闲项.
					break;
			}
		}
		// 如果没有找到空闲 i 节点(inode = NULL), 则将 i 节点表打印出来供调试使用, 并停机.
		if (!inode) {
			for (i = 0; i < NR_INODE; i++)
				printk("%04x: %6d\t", inode_table[i].i_dev, inode_table[i].i_num);
			panic("No free inodes in mem");
		}
		// 等待该 i 节点解锁(如果又被上锁的话). 
		wait_on_inode(inode);
		// 如果该 i 节点已修改标志被置位的话, 则将该 i 节点刷新(同步). 
		// 因为刷新时可能会睡眠, 因此需要再次循环等待 i 节点解锁.
		while (inode->i_dirt) {
			write_inode(inode);
			wait_on_inode(inode);
		}
	// 如果 i 节点又被其他占用的话(i 节点的计数值不为 0 了), 则重新寻找空闲 i 节点. 
	} while (inode->i_count); 									// 循环直至找到空闲项.
	// 否则说明已找到符合要求的空闲 i 节点项. 则将该 i 节点项内容清零, 并置引用计数为 1, 返回该 i 节点指针.
	memset(inode, 0, sizeof(*inode));
	inode->i_count = 1;
	return inode;
}

// 获取管道节点. 
// 首先扫描 i 节点表, 寻找一个空闲 i 节点项, 然后取得一页空闲内存供管道使用. 
// 然后将得到的 i 节点的引用计数置为 2(读者和写者), 初始化管道头和尾, 置 i 节点的管道类型标志. 
// 返回 i 节点指针, 如果失败则返回 NULL. 
struct m_inode * get_pipe_inode(void)
{
	struct m_inode * inode;

	// 首先从内存 i 节点表中取得一个空闲 i 节点. 如果找不到空闲 i 节点则返回 NULL. 
	// 然后为该 i 节点申请一页内存, 并让节点的 i_size 字段指向该页面. 
	// 如果已没有空闲内存, 则释放该 i 节点, 并返回 NULL. 
	if (!(inode = get_empty_inode()))
		return NULL;
	if (!(inode->i_size = get_free_page())) {         			// 节点的 i_size 字段指向缓冲区. 
		inode->i_count = 0;
		return NULL;
	}
	// 然后设置该 i 节点的引用计数为 2, 并复位管道头尾指针. 
	// i 节点逻辑块号数组 i_zone[] 的 i_zone[0] 和 i_zone[1] 中分别用来存放管道头和管道尾指针. 
	// 最后设置 i 节点是管道 i 节点标志并返回该 i 节点号. 
	inode->i_count = 2;											/* sum of readers/writers */    /* 读/写两者总计 */
	PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0;      			// 复位管道头尾指针. 
	inode->i_pipe = 1;                              			// 置节点为管道使用标志. 
	return inode;
}

// 获取指定 dev 上指定 inode 号对应的 i 节点指针.
// 参数: dev - 设备号; nr - i 节点号.
// 从设备上读取指定节点号的 i 节点结构内容到内存 i 节点表中, 并且返回该 inode 指针. 
// 首先在 inode 列表中查找该 inode 是否已缓存过, 若找到指定的 inode, 
// 如果该 inode 是其它文件系统的挂载点(i_mount == 1)则查找并返回该文件系统的根 inode 指针,
// 如果不是其它文件系统的挂载点, 则直接返回该 inode 指针.
// 如果没有在 inode 列表中找到, 则从设备 dev 上读取指定 i 节点号的 inode 信息放入 i 节点表中, 并返回该 i 节点指针.
struct m_inode * iget(int dev, int nr)
{
	struct m_inode * inode, * empty;

	// 首先判断参数有效性. 若设备号是 0, 则表明内核代码问题, 显示出错信息并停机. 
	// 然后预先从 i 节点表中取一个空闲 i 节点备用.
	if (!dev)
		panic("iget with dev == 0");
	empty = get_empty_inode(); 							// 预先从 inode_table 中获取一个空闲项.
	// 接着扫描 i 节点表. 寻找指定设备 dev 及节点号 nr 对应的 i 节点. 并递增该节点的引用次数. 
	inode = inode_table;
	while (inode < NR_INODE + inode_table) {
		// 如果当前扫描 i 节点的设备号 dev 不等于指定的设备号或者节点号 nr 不等于指定的节点号, 则继续扫描.
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode++;
			continue;
		}
		// 如果在 inode 列表中找到指定设备号 dev 和节点号 nr 的 i 节点, 则等待该节点解锁(如果已上锁的话). 
		// 在等待该节点解锁过程中, i 节点内容可能会发生变化. 所以再次进行上述相同判断. 
		// 如果发生了变化, 则重新扫描整个 i 节点表.
		wait_on_inode(inode);
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode = inode_table;
			continue;
		}
		// 到这里表示找到指定设备及节点号对应的 i 节点. 于是将该 i 节点引用计数加 1. 
		// 然后再作进一步检查, 看它是否为要查找的文件系统(超级块)的安装点. 若是则寻找被安装文件系统根节点并返回. 
		// 如果该 i 节点的确是其他文件系统的安装点, 则在超级块表中搜寻安装在此 i 节点的超级块. 
		// 如果没有找到, 则显示出错信息, 并放回本函数开始时获取的空闲节点 empty, 返回该 i 节点指针.
		inode->i_count++;
		// 当另一个文件系统挂载到了这个 inode 上(只有挂载 i_mount 才会置位), 这个 inode 就不是普通的 inode 了, 
		// 它在超级块表中就有一个对应的超级块, 我们需要通过这个超级块获取挂载到这个 inode 的物理设备号, 并获取这个文件系统的根 inode.
		if (inode->i_mount) { 								// 该 inode 是否挂载了其它文件系统.
			int i;

			for (i = 0; i < NR_SUPER; i++)
				if (super_block[i].s_imount == inode) 		// 如果某个文件系统(超级块)挂载到了这个 i 节点.
					break;
			// 如果没有超级块(文件系统)安装到这个 i 节点, 那么这个 inode 还是一个普通的 inode, 直接返回它.
			if (i >= NR_SUPER) { 							
				printk("Mounted inode hasn't got super block.\n");
				if (empty) 									// 将之前获取的空闲 inode 项释放.
					iput(empty);
				return inode;
			}
			// 执行到这里表示已经找到挂载到该 inode 节点的文件系统的超级块. 
			// 于是将该 i 节点写盘放回, 并从挂载到这个 inode 的文件系统的超级块中获取设备号, 并令 i 节点号为 ROOT_INO. 
			// 然后重新扫描整个 i 节点表, 以获取该被挂载的文件系统的根 i 节点信息.
			iput(inode);
			dev = super_block[i].s_dev; 					// 
			nr = ROOT_INO;
			inode = inode_table;
			continue;
		}
		// 最终我们找到了缓存的 i 节点. 因此可以放弃本函数开始处临时的空闲 i 节点, 返回找到的 i 节点指针.
		if (empty)
			iput(empty);
		return inode;
    }
	// 如果我们在 i 节点表中没有找到指定的 i 节点, 则设置前面申请的空闲 i 节点项 empty, 
	// 然后从对应设备上读取该 i 节点信息, 最后返回该 i 节点指针.
	if (!empty) 										// 如果没有申请到空闲项, 则返回 NULL.
		return (NULL);
	inode = empty;
	inode->i_dev = dev;									// 设置 i 节点的设备.
	inode->i_num = nr;									// 设置 i 节点号.
	read_inode(inode);      							// 读取指定设备号及节点号的 i 节点信息.
	return inode;
}

// 读取指定 i 节点信息.
// 从设备上读取含有指定 i 节点信息的 i 节点盘块, 然后复制到指定的 i 节点结构中. 
// 为了确定 i 节点所在设备逻辑块号(或缓冲块), 必须首先读取相应设备上的超级块, 
// 以获取用于计算逻辑块号的每块 i 节点数信息 INODES_PER_BLOCK. 
// 在计算出 i 节点所在的逻辑块号后, 就把该逻辑块读入一缓冲块中. 
// 然后把缓冲块中相应位置处的 i 节点内容复制到指定的位置处.
static void read_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	// 首先锁定该 i 节点, 并取该节点所在设备的超级块.
	lock_inode(inode);
	if (!(sb = get_super(inode->i_dev)))
		panic("trying to read inode without dev");
	// 该 i 节点所在设备逻辑块号 = (引导块(1) + 超级块(1)) + i 节点位图占用的块数 + 逻辑块位图的块数 + ((i 节点号 - 1) / 每块含有的 i 节点数).
	// 虽然 i 节点号从 0 开始编号, 但第 1 个 0 号 i 节点不用, 并且磁盘上也不保存对应的 0 号 i 节点结构. 
	// 因此存放 i 节点的盘块上第 1 块上保存的是 i 节点号是 1--32 的 i 节点结构而不是 0--31 的. 
	// 因此在上面计算 i 节点号对应的 i 节点结构所在盘块时需要减 1, 即: B = (i 节点号 - 1) / 每块含有 i 节点结构数. 
	// 例如, 节点号 32 的 i 节点结构应该在 B = (32 - 1) / 32 = 0 的块上. 
	// 这里我们从设备上读取该 i 节点所在逻辑块, 并复制指定 i 节点内容到 inode 指针所指位置处.
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks + ((inode->i_num - 1) / INODES_PER_BLOCK);
	// 将 i 节点信息所在的逻辑块读取到高速缓存中.
	if (!(bh = bread(inode->i_dev, block)))
		panic("unable to read i-node block");
	// 复制磁盘上相应的 inode 信息到内存中(只复制指定的那个 inode 信息).
	*(struct d_inode *)inode = ((struct d_inode *)bh->b_data)[(inode->i_num - 1) % INODES_PER_BLOCK]; 	// 求余得到在该页面内的下标.
	// 最后释放读入的缓冲块, 并解锁该 i 节点. 
	brelse(bh);
	// 对于块设备文件(比如 /dev/fd0), 还需要设置 i 节点的文件最大长度值.
	if (S_ISBLK(inode->i_mode)) {
		int i = inode->i_zone[0];							// 对于块设备文件, i_zone[0] 中是设备号.
		if (blk_size[MAJOR(i)])
			inode->i_size = 1024 * blk_size[MAJOR(i)][MINOR(i)];
		else
			inode->i_size = 0x7fffffff;
	}
	unlock_inode(inode);
}

// 将 i 节点信息写入缓冲区中.
// 该函数把参数指定的 i 节点写入缓冲区相应的缓冲块中, 待缓冲区刷新时会写入盘中. 
// 为了确定 i 节点所在的设备逻辑块号(或缓冲块), 必须首先读取相应设备上的超级块,
// 以获取用于计算逻辑块号的每块 i 节点数信息 INODES_PER_BLOCK. 
// 在计算出 i 节点所在的逻辑块号后, 就把该逻辑块读入缓冲块中. 然后把 i 节点内容复制到缓冲块的相应位置处.
static void write_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	// 首先锁定该 i 节点, 如果该 i 节点没有被修改过或者该 i 节点的设备号等于零, 则解锁该 i 节点, 并退出. 
	// 对于没有被修改过的 i 节点, 其内容与缓冲区中或设备中的相同. 然后获取该 i 节点的超级块.
	lock_inode(inode);
	if (!inode->i_dirt || !inode->i_dev) {
		unlock_inode(inode);
		return;
	}
	if (!(sb = get_super(inode->i_dev)))
		panic("trying to write inode without device");
	// 该 i 节点所在的设备逻辑号 = (启动块 + 超级块) + i 节点位图占用的块数 + 逻辑块位图占用的块数 + (i 节点号 - 1) / 每块含有的 i 节点数. 
	// 我们从设备上读取该i节点所在的逻辑块, 并将该 i 节点信息复制到逻辑块对应该 i 节点的项位置处.
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
		(inode->i_num - 1) / INODES_PER_BLOCK;
	if (!(bh = bread(inode->i_dev, block)))
		panic("unable to read i-node block");
	((struct d_inode *)bh->b_data)
		[(inode->i_num - 1) % INODES_PER_BLOCK] =
			*(struct d_inode *)inode;
	// 然后置缓冲区已修改标志, 而 i 节点内容已经与缓冲区中的一致, 因此修改标志置零. 
	// 然后释放该含有 i 节点的缓冲区, 并解锁该 i 节点.
	bh->b_dirt = 1;
	inode->i_dirt = 0;
	brelse(bh);
	unlock_inode(inode);
}
