/*
 *  linux/fs/bitmap.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* bitmap.c contains the code that handles the inode and block bitmaps */
/* bitmap.c 程序含有处理 inode 和磁盘块位图的代码 */
#include <string.h>
#include <linux/sched.h>							// 调度程序头文件, 定义任务结构 task_struct, 任务 0 数据.
#include <linux/kernel.h>

// 将指定地址(addr)处的一块 1024 字节内存清零.
// 输入: eax = 0; ecx = 以字节为单位的数据块长度(BLOCK_SIZE / 4); edi = 指定起始地址 addr.
#define clear_block(addr) \
__asm__("cld\n\t"               		/* 清方向位 */\
		"rep\n\t" 						/* 重复执行存储数据(0) */\
		"stosl" \
		: : "a" (0), "c" (BLOCK_SIZE / 4), "D" ((long)(addr)))

// 把指定地址开始的第 nr 个位偏移处的位置位(nr 可大于 32). 返回原位值.
// 输入: %0 - eax(返回值); %1 - eax(0); %2 - nr, 位偏移值; %3 - (addr), addr 的内容.
// 定义了一个局部寄存器变量 res, 该变量将被保存在指定的 eax 寄存器中, 以便于高效访问和操作. 
// 这种定义变量的方法主要用于内嵌汇编程序中. 整个宏定义是一个语句表达式, 该表达式值是最后 res 的值. 
// btsl 指令用于测试并设置位(Bit Test and Set). 
// 把基地址(%3)和位偏移值(%2)所指定的位值先保存到进位标志 CF 中, 然后设置该位为 1.
// 指令 setb 用于根据进位标志 CF 设置操作数(%al). 如果 CF = 1 则 %al = 1, 否则 %al = 0.
#define set_bit(nr, addr) ({\
	register int res __asm__("ax"); \
	__asm__ __volatile__("btsl %2, %3\n\tsetb %%al" : "=a" (res) : "0" (0), "r" (nr), "m" (*(addr))); res;\
})

// 复位指定地址开始的第 nr 位偏移处的位. 返回原位值的反码. 
// 输入: %0 - eax(返回值); %1 - eax(0); %2 - nr, 位偏移值; %3 - (addr), addr 的内容. 
// btrl 指令用与测试并复位该位(Bit Test and Reset). 其作用与上面的 btsl 类似, 但是复位指定位. 
// 指令 setnb 用于根据进位标志 CF 设置操作数(%al). 如果 CF = 1 则 %al = 0, 否则 %al = 1. 
#define clear_bit(nr, addr) ({ \
	register int res __asm__("ax"); \
	__asm__ __volatile__("btrl %2, %3\n\tsetnb %%al" : "=a" (res) : "0" (0), "r" (nr), "m" (*(addr))); res; \
})

// 从 addr 开始寻找第 1 个 0 值位.
// 输入: %0 - ecx(返回值); %1 - ecx(0); %2 - esi(addr).
// 在 addr 指定地址开始的位图中寻找第 1 个是 0 的位, 并将其距离 addr 的位偏移值返回. 
// addr 是缓冲块数据区的地址, 扫描寻找的范围是 1024 字节(8192 位).
#define find_first_zero(addr) ({ \
	int __res; \
	__asm__(\
		"cld\n"                         				/* 清方向位 */\
		"1:\tlodsl\n\t" 								/* 取 [esi] -> eax */\
		"notl %%eax\n\t"                				/* eax 中每位取反 */\
		"bsfl %%eax, %%edx\n\t"          				/* 从位 0 扫描 eax 中是 1 的第 1 个位, 其偏移值 -> edx */\
		"je 2f\n\t"                     				/* 如果 eax 中全是 0, 则向前跳转到标号 2 处 */\
		"addl %%edx, %%ecx\n\t"          				/* 偏移值加入 ecx(ecx 是位图首个 0 值位的偏移值) */\
		"jmp 3f\n"                      				/* 向前跳转到标号 3 处(结束) */\
		"2:\taddl $32, %%ecx\n\t"        				/* 未找到 0 值位, 则将 ecx 加 1 个长字的位偏移量 32 */\
		"cmpl $8192, %%ecx\n\t"          				/* 已经扫描了 8192 位(1024 字节)了吗? */\
		"jl 1b\n"                       				/* 若还没有扫描完 1 块数据, 则向前跳转到标号 1 处结束. */\
		"3:"                            				/* 此时 ecx 中是位偏移量 */\
		: "=c" (__res) : "c" (0), "S" (addr) : "ax", "dx"); \
	__res; \
})

// 释放设备 dev 上数据区中的逻辑块 block. 
// 复位指定逻辑块 block 对应的逻辑块位图位. 成功则返回 1, 否则返回 0.
// 参数: dev 是设备号, block 是逻辑块号(盘块号). 
int free_block(int dev, int block) {
	struct super_block * sb;
	struct buffer_head * bh;

	// 首先取设备 dev 上文件系统的超级块信息, 根据其中数据区开始逻辑块号和文件系统中逻辑块总数信息判断参数 block 的有效性. 
	// 如果指定设备超级块不存在, 则出错停机. 若逻辑块号小于盘上数据区第 1 个逻辑块号或者大于设备上总逻辑块数, 也出错停机. 
	if (!(sb = get_super(dev))) {            						// fs/super.c
		panic("trying to free block on nonexistent device");
	}
	if (block < sb->s_firstdatazone || block >= sb->s_nzones) {
		panic("trying to free block not in datazone");
	}
	bh = get_hash_table(dev, block);
	// 然后从 hash 表中寻找该块数据. 若找到了则判断其有效性, 并清已修改和更新标志, 释放该数据块. 
	// 该段代码的主要用途是如果该逻辑块目前存在于高速缓冲区中, 就释放对应的缓冲块. 
	if (bh) {
		if (bh->b_count > 1) {          							// 如果引用次数大于 1, 则调用 brelse(). 
			brelse(bh);             								// b_count -- 后退出, 该块还有人用. 
			return 0;
		}
		bh->b_dirt = 0;                   							// 否则复位已修改和已更新标志. 
		bh->b_uptodate = 0;
		if (bh->b_count) {               							// 若此时 b_count 为 1, 则调用 brelse() 释放之. 
			brelse(bh);
		}
	}
	// 接着我们复位 block 在逻辑块位图中的位(置 0). 先计算 block 在数据区开始算起的数据逻辑块号(从 1 开始计数). 
	// 然后对逻辑块(区块)位图进行操作, 复位对应的位. 如果对应位原来就是 0, 则出错停机. 由于 1 个缓冲块有 1024 字节, 即 8192 位, 
	// 因此 block/8192 即可计算出指定块 block 在逻辑位图中的哪个块上. 而 block & 8191 可以得到 block 在逻辑块位图当前块中的位偏移位置. 
	block -= sb->s_firstdatazone - 1 ;
	if (clear_bit(block & 8191, sb->s_zmap[block / 8192]->b_data)) {
		printk("block (%04x:%d)", dev, block + sb->s_firstdatazone - 1);
		printk("free_block: bit already cleared\n");
	}
	// 最后置相应逻辑块位图所在缓冲区已修改标志. 
	sb->s_zmap[block / 8192]->b_dirt = 1;
	return 1;
}

// 向设备申请一个逻辑块(盘块, 区块).
// 函数首先取得设备的超级块, 并在超级块中的逻辑块位图中寻找第一个 0 值位(代表一个空闲逻辑块). 
// 然后置位对应逻辑块在逻辑位图中的位. 接着为该逻辑块在缓冲区中取得一块对应缓冲块. 最后将该缓冲块清零, 并设置其已更新标志和已修改标志. 
// 并返回逻辑块号. 函数执行成功则返回逻辑块号(盘块号), 否则返回 0.
int new_block(int dev) {
	struct buffer_head * bh;
	struct super_block * sb;
	int i, j;

	// 首先获取设备 dev 的超级块. 如果指定设备的超级块不存在, 则出错停机. 
	if (!(sb = get_super(dev))) {
		panic("trying to get new block from nonexistant device");
	}
	// 然后扫描文件系统的 8 块逻辑块位图, 查找空闲逻辑块. 
	j = 8192;
	for (i = 0; i < 8; i++) {
		if (bh = sb->s_zmap[i]) {
			if ((j = find_first_zero(bh->b_data)) < 8192) {
				break;	// 查找空闲逻辑块.
			}
		}
	}
	// 如果 8 块逻辑块位图的所有位都没有 0 值, 则返回 0 退出(没有空闲逻辑块).
	if (i >= 8 || !bh || j >= 8192) {
		return 0;
	}
	// 在块位图中置位空闲逻辑块 j, 若对应位已经置位, 则出错停机. 
	if (set_bit(j, bh->b_data)) {
		panic("new_block: bit already set");
	}
	// 设置逻辑块位图所在的缓冲区块已修改标志. 因为逻辑块位图仅表示盘上数据区中逻辑块的占用情况, 
	bh->b_dirt = 1;
	// 即逻辑块位图中位偏移值表示从数据区开始处算起的块号, 因此这里需要加上数据区第 1 个逻辑块的块号, 把 j 转换成逻辑块号. 
	j += i * 8192 + sb->s_firstdatazone - 1;
	// 此时如果新逻辑块号大于该设备上的总逻辑块数, 则说明指定逻辑块在对应设备上不存在. 申请失败, 返回 0 退出.
	if (j >= sb->s_nzones) {
		return 0;
	}
	// 然后在高速缓冲区中为该设备上指定的逻辑块号取得一个缓冲块, 并返回缓冲块头指针.
	if (!(bh = getblk(dev, j))) {
		panic("new_block: cannot get block");
	}
	// 因为刚取得的逻辑块其引用次数一定为 1(getblk() 中会设置), 因此若不为 1 则出错停机. 
	// 最后将新逻辑块的数据缓冲区清空, 并设置已更新和已修改标志. 然后释放对应缓冲块, 返回逻辑块号.
	if (bh->b_count != 1) {
		panic("new block: count is != 1");
	}
	// 之所以申请后又释放, 主要目的是设置更新标志和脏标志, 让其它进程使用的时候需要进行相应处理.
	clear_block(bh->b_data);
	bh->b_uptodate = 1;
	bh->b_dirt = 1;
	brelse(bh);
	return j;
}

// 释放指定的 inode. 
// 该函数首先判断参数给出的 inode 号的有效性和可释放性. 若 inode 仍然在使用中则不能被释放. 
// 然后利用超级块信息对 inode 位图进行操作, 复位 inode 号对应的 inode 位图中位, 并清空 inode 结构. 
void free_inode(struct m_inode * inode) {
	struct super_block * sb;
	struct buffer_head * bh;

	// 首先判断参数给出的需要释放的 inode 有效性或合法性. 如果 inode 指针 = NULL, 则退出. 
	// 如果 inode 上的设备号字段为 0, 说明该节点没有使用. 于是用 0 清空对应 inode 所占内存区并返回. 
	// 这里表示用 0 填写 inode 指针指定处, 长度是 sizeof(*inode) 的内存块. 
	if (!inode) return;
	if (!inode->i_dev) {
		memset(inode, 0, sizeof(*inode));										// memset() 定义在 include/string.h 处. 
		return;
	}
	// 如果此 inode 还有其他程序引用, 则不释放, 说明内核有问题, 停机. 
	// 如果文件连接数不为 0, 则表示还有其他文件目录项在使用该节点, 因此也不应释放, 而应该放回等. 
	if (inode->i_count > 1) {
		printk("trying to free inode with count=%d\n", inode->i_count);
		panic("free_inode");
	}
	if (inode->i_nlinks) {
		panic("trying to free inode with links");
	}
	// 在判断完 inode 的合理性之后, 我们开始利用其超级块信息对其 inode 位图进行操作. 
	// 首先取 inode 所在设备的超级块, 测试设备是否存在. 
	// 然后判断 inode 号的范围是否正确, 如果 inode 号等于 0 或大于该设备上 inode 总数, 则出错(0 号 inode 保留没有使用). 
	// 如果该 inode 对应的节点位图不存在, 则出错. 因为一个缓冲块的 inode 位图有 8192 比特位. 
	// 因此 i_num >> 13(即 i_num/8192)可以得到当前 inode 号所在的 s_imap[] 项, 即所在盘块. 
	if (!(sb = get_super(inode->i_dev))) {
		panic("trying to free inode on nonexistent device");
	}
	if (inode->i_num < 1 || inode->i_num > sb->s_ninodes) {
		panic("trying to free inode 0 or nonexistant inode");
	}
	if (!(bh = sb->s_imap[inode->i_num >> 13])) {
		panic("nonexistent imap in superblock");
	}
	// 现在我们复位 inode 对应的节点位图中的位. 如果该位已经等于 0, 则显示出错警告信息. 
	// 最后置 inode 位图所在缓冲区已修改标志, 并清空该 inode 结构所占内存区. 
	if (clear_bit(inode->i_num & 8191, bh->b_data)) {
		printk("free_inode: bit already cleared.\n\r");
	}
	bh->b_dirt = 1;
	memset(inode, 0, sizeof(*inode));
}

// 为设备 dev 建立一个新 inode. 初始化并返回该新 inode 的指针. 
// 在内存 inode 表中获取一个空闲 inode 表项, 并从 inode 位图中找一个空闲 inode. 
struct m_inode * new_inode(int dev) {
	struct m_inode * inode;
	struct super_block * sb;
	struct buffer_head * bh;
	int i, j;

	// 首先从内在 inode 表(inode_table)中获取一个空闲 inode 项, 并读取指定设备的超级块结构. 
	// 然后扫描超级块中8块i节点位图, 寻找第 1 个 0 位, 寻找空闲节点, 获取放置该 inode 的节点号. 
	// 如果全部扫描完还没找到, 或者位图所在的缓冲块无效(bh = NULL)则放回先前申请的 inode 表中的 inode , 
	// 并返回空指针退出(没有空闲 inode ). 
	if (!(inode = get_empty_inode())) {
		return NULL;
	}
	if (!(sb = get_super(dev))) {
		panic("new_inode with unknown device");
	}
	j = 8192;
	for (i = 0; i < 8; i++) {
		if (bh = sb->s_imap[i]) {
			if ((j = find_first_zero(bh->b_data)) < 8192) {
				break;
			}
		}
	}
	if (!bh || j >= 8192 || j + i * 8192 > sb->s_ninodes) {
		iput(inode);
		return NULL;
	}
	// 现在我们已经找到了还未使用的 inode 号 j. 于是置位 inode  j 对应的 inode 位图相应比特位(如果已经置位, 则出错). 
	// 然后置 inode 位图所在缓冲块已修改标志. 最后初始化该 inode 结构(i_ctime 是 inode 内容改变时间). 
	if (set_bit(j, bh->b_data)) {
		panic("new_inode: bit already set");
	}
	bh->b_dirt = 1;
	inode->i_count = 1;               										// 引用计数. 
	inode->i_nlinks = 1;              										// 文件目录项链接数. 
	inode->i_dev = dev;               										// i节点所在的设备号. 
	inode->i_uid = current->euid;     										// inode 所属用户 id. 
	inode->i_gid = current->egid;     										// 组 id. 
	inode->i_dirt = 1;                										// 已修改标志置位. 
	inode->i_num = j + i * 8192;      										// 对应设备中的 inode 号. 
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;        // 设置时间. 
	return inode;                   										// 返回该 inode 指针. 
}
