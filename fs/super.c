/*
 *  linux/fs/super.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
/*
 * super.c 程序中含有处理超级块表的代码. 
 */
//#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include <errno.h>
#include <sys/stat.h>

int sync_dev(int dev);                  		// 对指定设备执行高速缓冲与设备上数据同步操作(fs/buffer.c).
void wait_for_keypress(void);           		// 等待击键(kernel/chr_drv/tty_io.c)

/* set_bit uses setb, as gas doesn't recognize setc */
/* set_bit() 使用了 setb 指令, 因为汇编编译器 gas 不能识别指令 setc */
// 测试指定位偏移处位的值, 并返回该原位值(应该取名为 test_bit() 更妥帖).
// 嵌入式汇编宏. 参数 bitnr 是位偏移值, addr 是测试位操作的起始地址.
// %0 - ax(__res), %1 - 0, %2 - bitnr, %3 - addr
// __res 是定义了一个局部寄存器变量. 该变量将被保存在 eax 寄存器中, 以便于高效访问和操作. 
// `bt` 指令用于对位进行测试(Bit Test). 
// 它会把地址 addr(%3) 和位偏移量 bitnr(%2) 指定的位的值放入进位标志 CF 中. 
// 指令 setb 用于根据进位标志 CF 设置操作数 %al. 如果 CF = 1 则 %al = 1, 否则 %al = 0.
#define set_bit(bitnr, addr) ({ \
register int __res; \
__asm__("bt %2, %3; setb %%al":"=a" (__res):"a" (0),"r" (bitnr),"m" (*(addr))); __res; })

struct super_block super_block[NR_SUPER];					// 超级块结构表数组(NR_SUPER = 8)
/* this is initialized in init/main.c */
/* ROOT_DEV 已在 init/main.c 中被初始化. */
int ROOT_DEV = 0;											// 根文件系统设备号.

// 以下 3 个函数(lock_super(), free_super() 和 wait_on_super()) 的作用与 inode.c 文件中头 ３ 个函数的作用相同, 
// 只是这里操作的对象换成了超级块.

// 锁定超级块.
// 如果超级块已被锁定, 则将当前任务置为不可中断的等待状态, 并添加到该超级块等待队列 s_wait 中. 
// 直到该超级块解锁并明确地唤醒本任务, 然后对其上锁.
static void lock_super(struct super_block * sb)
{
	cli();													//　关中断
	while (sb->s_lock)										//　如果该超级块已经上锁, 则睡眠等待.
		sleep_on(&(sb->s_wait));
	sb->s_lock = 1;											//　给该超级块加锁(置锁定标志)
	sti();													//　开中断.
}

//　对指定超级块解锁
//　复位超级块的锁定标志, 并明确地唤醒等待此超级块等待队列 s_wait 上的所有进程.
//　如果使用 unlock_super 这个名称则可能更妥帖.
static void free_super(struct super_block * sb)
{
	cli();
	sb->s_lock = 0;											// 复位锁定标志.
	wake_up(&(sb->s_wait));									// 唤醒等待该超级块的进程.
	sti();													// wake_up() 在 kernel/sched.c
}

// 睡眠等待超级块解锁.
// 如果超级块已被锁定, 则将当前任务置为不可中断的等待状态, 
// 并添加到该超级块的等待队列 s_wait 中, 直到该超级块解锁并明确地唤醒本任务.
static void wait_on_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)										// 如果超级块已经上锁, 则睡眠等待.
		sleep_on(&(sb->s_wait));
	sti();
}

// 获取指定设备的超级块.
// 在超级块表(数组)中搜索指定设备 dev 的超级块结构信息. 若找到则返回超级块的指针, 否则返回空指针.
struct super_block * get_super(int dev)
{
	struct super_block * s;									// s 是超级块数据结构指针.

	// 首先判断参数给出设备的有效性. 若设备号为 0 则返回空指针. 
	// 然后让 s 指向超级块数组起始处, 开始搜索整个超级块数组, 以寻找指定设备 dev 的超级块.
	// 下面的指针赋值语句 `s = 0 + super_block` 等同于 `s = super_block` 或 `s = &super_block[0]`.
	if (!dev)
		return NULL;
	s = 0 + super_block;
	while (s < NR_SUPER + super_block) {
		// 如果当前搜索项是指定设备的超级块, 即该超级块的设备号字段值与函数参数指定的相同, 
		// 则先等待该超级块解锁(若已被其他进程上锁的话).
		// 在等待期间, 该超级块项有可能被其他设备使用, 
		// 因此等待返回之后需再判断一次是否是指定设备的超级块, 如果是则返回该超级块的指针. 
		// 否则就重新对超级块数组再搜索一遍, 因此此时 s 需重又指向超级块数组开始处.
		if (s->s_dev == dev) {
			wait_on_super(s);
			if (s->s_dev == dev)
				return s;
			s = 0 + super_block;
		// 如果当前搜索项不是, 则检查下一项. 如果没有找到指定的超级块, 则返回空指针.
		} else {
			s++;
		}
	}
	return NULL;
}

// 释放(放回)指定设备的超级块.
// 释放设备所使用的超级块数组项(置 s_dev = 0), 并释放该设备 i 节点位图和逻辑块位图所占用的高速缓冲块. 
// 如果超级块对应的文件系统是根文件系统, 或者其某个 i 节点上已经安装了其他的文件系统, 则不能释放该超级块.
void put_super(int dev)
{
	struct super_block * sb;
	int i;

	// 首先判断参数的有效性和合法性. 如果指定设备是根文件系统设备, 则显示警告信息 “根系统盘改变了, 准备生死决战吧”, 并返回. 
	// 然后在超级块表中寻找指定设备号的文件系统超级块. 如果找不到指定设备超级块, 则返回. 
	// 另外, 如果该超级块指明该文件系统所安装到的 i 节点还没有被处理过, 则显示警告信息并返回. 
	// 在文件系统卸载(umount)操作中, s_imount 会先被置成 NULL 以后才会调用本函数.
	if (dev == ROOT_DEV) {
		printk("root diskette changed: prepare for armageddon\n\r");
		return;
	}
	if (!(sb = get_super(dev)))
		return;
	if (sb->s_imount) {
		printk("Mounted disk changed - tssk, tssk\n\r");
		return;
	}
	// 然后在找到指定设备的超级块之后, 我们先锁定该超级块, 再置该超级块对应的设备号字段 s_dev 为 0, 
	// 也即释放该设备上的文件系统超级块. 然后释放该超级块占用的其他内核资源, 
	// 即释放该设备上文件系统 i 节点位图和逻辑位图在缓冲区中所占用的缓冲块. 
	// 下面常数符号 I_MAP_SLOTS 和 Z_MAP_SLOTS 均等于 8, 用于分别指明 i 节点位图和逻辑块位图占用的磁盘逻辑块数. 
	// 注意, 若这些缓冲块内容被修改过, 则需要作同步操作才能把缓冲块中的数据写入设备中. 函数最后对该超级块, 并返回.
	lock_super(sb);
	sb->s_dev = 0;                          		// 置超级块空闲.
	for(i = 0; i < I_MAP_SLOTS; i++)
		brelse(sb->s_imap[i]);
	for(i = 0; i < Z_MAP_SLOTS; i++)
		brelse(sb->s_zmap[i]);
	free_super(sb);
	return;
}

// 读取指定设备的超级块.
// 如果指定设备 dev 上的文件系统超级块已经在超级块表中, 则直接返回该超级块项的指针. 
// 否则就从设备 dev 上读取超级块到缓冲块中, 并复制到超级块表中, 并返回超级块指针.
static struct super_block * read_super(int dev)
{
	struct super_block * s;
	struct buffer_head * bh;
	int i, block;

	// 首先判断参数的有效性. 如果没有指明设备, 则返回空指针. 否则检查该设备是否更换过盘片(也即是否是软盘设备). 
	// 如果更换过盘, 则高速缓冲区有关该设备的所有缓冲块均失效, 需要进行失效处理, 即释放原来加载的文件系统.
	if (!dev)
		return NULL;
	check_disk_change(dev);
	// 如果设备的超级块已经在超级块表中, 则直接返回该超级块的指针. 
	// 否则, 首先在超级块数组中找出一个空项(也即字段 s_dev = 0 的项). 如果数组已经占满则返回空指针.
	if (s = get_super(dev))
		return s;
	for (s = 0 + super_block; ; s++) {
		if (s >= NR_SUPER + super_block)
			return NULL;
		if (!s->s_dev) 									// 找到空闲项.
			break;
	}
	// 在超级块数组中找到空闲项后, 就将该超级块项用于指定设备 dev 上的文件系统. 
	// 于是对该超级块结构中的内存字段进行部分初始化处理.
	s->s_dev = dev;										// 指定该超级块所属的 dev 设备号.
	s->s_isup = NULL;
	s->s_imount = NULL;
	s->s_time = 0;
	s->s_rd_only = 0; 									// 只读标志.
	s->s_dirt = 0; 										// 已修改(脏)标志.
	// 然后锁定该超级块, 并从设备上读取设备上的超级块信息到 bh 指向的缓冲块中. 
	// 超级块位于块设备的第 1 个逻辑块中, (第 0 个是引导块). (每个块大小为 1KB).
	// 如果读超级块操作失败, 则释放上面选定的超级块数组中的项(即置 s_dev = 0), 并解锁该项, 返回空指针退出. 
	// 否则就将设备上读取的超级块信息从缓冲块数据区复制到超级块数组相应项结构中. 并释放存放读取信息的高速缓冲块.
	lock_super(s);
	if (!(bh = bread(dev, 1))) { 						// 从设备中读取超级块(1).
		s->s_dev = 0;
		free_super(s);
		return NULL;
	}
	// 把从硬盘中读取的超级块信息复制到超级块列表的对应项中.
	*((struct d_super_block *) s) = *((struct d_super_block *) bh->b_data);
	brelse(bh);
	// 现在我们从设备 dev 上得到了文件系统的超级块, 
	// 于是开始检查这个超级块的有效性并从设备上读取 i 节点位图和逻辑块位图等信息. 
	// 如果所读取的超级块的文件系统魔数字段不对, 说明设备上不是正确的文件系统, 因此向上面一样, 
	// 释放上面选定的超级块数组中的项, 并解锁该项, 返回空指针退出. 
	// 对于该版 Linux 内核, 只支持 MINIX 文件系统 1.0 版本, 其魔数是 0x137f.
	if (s->s_magic != SUPER_MAGIC) {
		s->s_dev = 0;
		free_super(s);
		return NULL;
	}
	// 下面开始读取设备上 i 节点位图和逻辑块位图数据. 首先初始化内存超级块结构中位图空间. 
	// 然后从设备上读取 i 节点位图和逻辑块位图信息, 并存放在超级块对应字段中. 
	// i 节点位图保存在设备的 2 号(第 3 个)逻辑块中, 共占用 s_imap_blocks(保存在超级块中) 个块. 
	// 逻辑块位图在 i 节点位图所在块的后续块中, 共占用 s_zmap_blocks 个块.
	for (i = 0; i < I_MAP_SLOTS; i++)					// 初始化操作.
		s->s_imap[i] = NULL;
	for (i = 0; i < Z_MAP_SLOTS; i++)
		s->s_zmap[i] = NULL;
	block = 2; 			// 2 号块保存 i 节点位图(引导块 - 超级块 - i 节点位图 - 逻辑块位图 - i 节点 - 数据区).
	// 从第二逻辑块号(2 号块)开始读取 i 节点位图.
	// 读取块设备中 i 节点位图信息到高速缓冲区, 并设置超级块信息指向这些缓冲区.
	for (i = 0; i < s->s_imap_blocks; i++)
		if (s->s_imap[i] = bread(dev, block)) 			// 从设备中依次读取各个 i 节点位图到高速缓冲区.
			block++;
		else
			break;
	// 从 i 节点位图之后开始读取逻辑块位图.
	for (i = 0; i < s->s_zmap_blocks; i++)				// 读取设备中逻辑块位图.
		if (s->s_zmap[i] = bread(dev, block))			// 从设备中依次读取各个 i 节点位图到高速缓冲区.
			block++;
		else
			break;
	// 如果读出的位图个数不等于位图应该占有的逻辑块数, 说明文件系统位图信息有问题, 超级块初始化失败. 
	// 因此只能释放前面申请并占用的所有资源, 即释放 i 节点位图和逻辑块位图占用的高速缓冲块, 
	// 释放上面选定的超级块数组项, 解锁该超级块项, 并返回空指针退出.
	if (block != 2 + s->s_imap_blocks + s->s_zmap_blocks) {
		for(i = 0; i < I_MAP_SLOTS; i++)				// 释放位图占用的高速缓冲块.
			brelse(s->s_imap[i]);
		for(i = 0; i < Z_MAP_SLOTS; i++)
			brelse(s->s_zmap[i]);
		s->s_dev = 0;									// 释放选定的超级块数组项.
		free_super(s);									// 解锁该超级块项.
		return NULL;
	}
	// 否则一切成功. 另外, 由于对于申请空闲 i 节点的函数来讲, 如果设备所有的 i 节点已经全被使用, 则查找函数会返回 0 值.
	// 因此 0 号 i 节点是不能用的, 所以这里将位图中第 1 块的最低位设置为 1, 以防止文件系统分配 0 号 i 节点.
	// 同样的道理, 也将逻辑块位图的最低位设置为 1. 最后函数解锁该超级块, 并返回超级块指针.
	s->s_imap[0]->b_data[0] |= 1;
	s->s_zmap[0]->b_data[0] |= 1;
	free_super(s);
	return s;
}

// 卸载文件系统(系统调用).
// 参数 dev_name 是文件系统所在设备的设备文件名.
// 该函数首先根据参数给出的块设备文件名获得设备号, 然后复位文件系统超级块中的相应字段, 释放超级块和位图占用的缓冲块, 
// 最后对该设备执行高速缓冲与设备上数据的同步操作. 若卸载操作成功则返回 0, 否则返回出错码.
int sys_umount(char * dev_name)
{
	struct m_inode * inode;
	struct super_block * sb;
	int dev;

	// 首先根据设备文件名找到对应的 i 节点, 并取其中的设备号. 设备文件所定义设备号是保存在其 i 节点的 i_zone[0] 中的. 
	// 参见后面 namei.c 程序中系统调用 sys_mknod() 的代码第 445 行. 另外, 由于文件系统需要存放在块设备上, 
	// 因此如果不是块设备文件, 则放回刚申请的 i 节点 dev_i, 返回出错码.
	if (!(inode = namei(dev_name)))
		return -ENOENT;
	dev = inode->i_zone[0];
	if (!S_ISBLK(inode->i_mode)) {
		iput(inode);                    				// fs/inode.c 
		return -ENOTBLK;
	}
	// OK, 现在上面为了得到设备号而取得的 i 节点已完成了它的使命, 因此这里放回该设备文件的 i 节点. 
	// 接着我们来检查一下卸载该文件系统的条件是否满足. 如果设备上是根文件系统, 则不能卸载, 返回忙出错号. 
	iput(inode);
	if (dev == ROOT_DEV)
		return -EBUSY;
	// 如果在超级块表中没有找到该设备上文件系统的超级块, 或者已找到但是该设备上文件系统没有安装过, 则返回出错码. 
	// 如果超级块所指明被安装到的 i 节点并没有置位其安装标志 i_mount, 则显示警告信息. 
	// 然后查找一下 i 节点表, 看看是否有进程在使用该设备上的文件, 如果有则返回忙出错码.
	if (!(sb = get_super(dev)) || !(sb->s_imount))
		return -ENOENT;
	if (!sb->s_imount->i_mount)
		printk("Mounted inode has i_mount=0\n");
	for (inode = inode_table + 0 ; inode < inode_table + NR_INODE ; inode++)
		if (inode->i_dev == dev && inode->i_count)
				return -EBUSY;
	// 现在该设备上文件系统的卸载条件均得到满足, 因此我们可以开始实施真正的卸载操作了. 
	// 首先复位被安装到的 i 节点的安装标志, 释放该 i 节点. 
	// 然后置超级块中被安装 i 节点字段为空, 并放回设备文件系统的根 i 节点, 接着置超级块中被安装系统根 i 节点指针为空.
	sb->s_imount->i_mount = 0;
	iput(sb->s_imount);
	sb->s_imount = NULL;
	iput(sb->s_isup);
	sb->s_isup = NULL;
	// 最后我们释放该设备上的超级块以及位图占用的高速缓冲块, 并对该设备执行高速缓冲与设备上数据的同步操作. 
	// 然后返回 0(卸载成功).
	put_super(dev);
	sync_dev(dev);
	return 0;
}

// 安装文件系统(系统调用).
// 参数 dev_name 是设备文件名, dir_name 是安装到的目录名, rw_flag 被安装文件系统的可读写标志. 
// 将被加载的地方必须是一个目录名, 并且对应的 i 节点没有被其他程序占用. 若操作成功则返回 0, 否则返回出错号.
int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
	struct m_inode * dev_i, * dir_i;
	struct super_block * sb;
	int dev;

	// 首先根据设备文件名找到对应的 i 节点, 以取得其中的设备号. 对于块特殊设备文件, 设备号在其 i 节点的 i_zone[0] 中. 
	// 另外, 由于文件系统必须在块设备中, 因此如果不是块设备文件, 则放回刚得的 i 节点 dev_i, 返回出错码.
	if (!(dev_i = namei(dev_name)))
		return -ENOENT;
	dev = dev_i->i_zone[0];
	if (!S_ISBLK(dev_i->i_mode)) {
		iput(dev_i);
		return -EPERM;
	}
	// OK, 现在上面为了得到设备号而取得的 i 节点 dev_i 已经完成了它的使命, 因此这里放回该设备文件的 i 节点. 
	// 接着我们来检查一下文件系统安装到的目录名是否有效. 于是根据给定的目录文件名找到对应的 i 节点 dir_i. 
	// 如果该 i 节点的引用计数不为 1(仅在这里引用), 或者该 i 节点的节点号是根文件系统的节点号 1, 
	// 则放回该 i 节点返回出错码. 
	// 另外, 如果该节点不是一个目录文件节点, 则也放回该 i 节点, 返回出错码. 因为文件系统只能安装在一个目录名上.
	iput(dev_i);
	if (!(dir_i = namei(dir_name)))
		return -ENOENT;
	if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
		iput(dir_i);
		return -EBUSY;
	}
	if (!S_ISDIR(dir_i->i_mode)) {          			// 安装点需要是一个目录名.
		iput(dir_i);
		return -EPERM;
	}
	// 现在安装点也检查完毕, 我们开始读取要安装文件系统的超级块信息. 
	// 如果读超级块操作失败, 则放回该安装点 i 节点 dir_i 并返回出错码. 
	// 一个文件系统的超级块会首先从超级块表中进行搜索, 如果不在超级块表中就从设备上读取.
	if (!(sb = read_super(dev))) {
		iput(dir_i);
		return -EBUSY;
	}
	// 在等到了文件系统超级块之后, 我们对它先进行检测一番. 
	// 如果将要被安装的文件系统已经安装在其他地方, 则放回该 i 节点, 返回出错码. 
	// 如果将要安装到的 i 节点已经安装了文件系统(安装标志已经置位), 则放回该i节点, 也返回出错码.
	if (sb->s_imount) {
		iput(dir_i);
		return -EBUSY;
	}
	if (dir_i->i_mount) {
		iput(dir_i);
		return -EPERM;
	}
	// 最后设置被安装文件系统超级块的 “被安装到 i 节点” 字段指向安装到的目录名的 i 节点. 
	// 并设置安装位置 i 节点的安装标志和节点已修改标志. 然后返回 0(安装成功).
	sb->s_imount = dir_i;
	dir_i->i_mount = 1;
	dir_i->i_dirt = 1;			/* NOTE! we don't iput(dir_i) */        /* 注意! 这里没有用 iput(dir_i) */
	return 0;					/* we do that in umount */      		/* 这将在 umount 内操作 */
}

// 安装根文件系统. 该函数属于系统初始化操作的一部分. 
// 函数首先初始化文件表数组 file_table[](fs/file_table.c) 和超级块表(数组), 
// 然后读取根文件系统超级块, 并取得文件系统根 i 节点. 
// 最后统计并显示出根文件系统上的可用资源(空闲块数和空闲 i 节点数). 
// 该函数会在系统开机进行初始化设置时(sys_setup())调用(kernel/blk_drv/hd.c).
void mount_root(void)
{
	int i, free;
	struct super_block * p;
	struct m_inode * mi;

	// 若磁盘 i 节点结构不是 32 字节, 则出错停机. 该判断用于防止修改代码时出现不一致情况.
	if (32 != sizeof (struct d_inode))
		panic("bad i-node size");
	// 首先初始化打开的文件表数组(共 64 项, 即系统只能同时打开 64 个文件)和超级块表. 
	// 这里将所有文件结构中的引用计数设置为 0(表示空闲), 并把超级块表中各项结构的设备字段初始化为 0(也表示空闲). 
	// 如果根文件系统所在设备是软盘的话, 就提示 "插入根文件系统盘, 并按回车键", 并等待按键.
	for (i = 0; i < NR_FILE; i++)					// 初始化文件表.
		file_table[i].f_count = 0; 					// (fs/file_table.c)
	if (MAJOR(ROOT_DEV) == 2) {						// 如果是 ROOT_DEV(根文件设备)是软盘, 则提示插入根文件系统盘.
		printk("Insert root floppy and press ENTER\r\n");
		wait_for_keypress();
	}
	// 初始化超级块列表.
	for (p = &super_block[0]; p < &super_block[NR_SUPER]; p++) {
		p->s_dev = 0;								// 超级块的设备号.
		p->s_lock = 0;
		p->s_wait = NULL;
	}
	// 做好以上 "分外" 的初始化工作之后, 我们开始安装根文件系统. 
	// 于是从根设备上读取文件系统超级块, 并取得文件系统的根 i 节点(0 号节点)在内存 i 节点表中的指针.
	// 如果读根设备上超级块失败或取根节点失败, 则显示错误信息并停机.
	if (!(p = read_super(ROOT_DEV))) 						// ROOT_DEV = 0x301. (硬盘的第一个分区)
		panic("Unable to mount root");
	if (!(mi = iget(ROOT_DEV, ROOT_INO)))					// 在 include/linux/fs.h 中 ROOT_INO 定义为 1.
		panic("Unable to read root i-node");
	// 现在我们对超级块和根 i 节点进行设置. 把根 i 节点引用次数递增 3 次. 
	// 因为随后 3 行代码也引用了该 i 节点. 并且 iget() 函数中 i 节点引用计数已被设置为 1. 
	// 然后置该超级块的被安装文件系统 i 节点和被安装到 i 节点字段为该 i 节点.
	// 再设置当前进程的当前工作目录和根目录 i 节点. 当前进程是 TASK-1(init 进程).
	mi->i_count += 3;								/* NOTE! it is logically used 4 times, not 1 */
                                					/* 注意! 从逻辑上讲, 它已被引用了 4 次, 而不是 1 次 */
	p->s_isup = p->s_imount = mi; 					// 设置根目录及超级块被安装到的 i 节点(根 i 节点).
	current->pwd = mi; 								// 设置当前任务的工作目录 i 节点(根 i 节点).
	current->root = mi; 							// 设置当前任务的根目录 i 节点(根 i 节点).
	// 然后我们对根文件系统上的资源进行统计. 统计该设备上空闲块数和空闲 i 节点数. 
	// 首先令 i 等于超级块中表明的设备逻辑块总数. 
	// 然后根据逻辑块位图中相应位的占用情况统计出空闲块数. 
	// 这里宏函数 set_bit() 只是在测试位, 而非设置位. 
	// "i & 8191" 用于取得 i 节点号在当前位图块中对应的位偏移值. 
	// (i & 0001-1111-1111-1111) 如果 0 <= i < 8191, 则 i & 8191 = i, 
	// 如果 i > 8191, 则 i & 8191 = (i % 8191 - 1), 比如 8192 & 8191 = 0;
	// "i >> 13" 是将 i 除以 8192, 也即除一个磁盘块包含的位数.
	free = 0; 														// 保存空闲的逻辑块数.
	i = p->s_nzones; 												// 该文件系统的总逻辑块个数.
	while (--i >= 0)
		// p->s_zmap[i >> 13]->b_data 得到该 i 节点所在逻辑块的位图数据(由上面的 get_super 函数加载到高速缓冲区中).
		if (!set_bit(i & 8191, p->s_zmap[i >> 13]->b_data))
			free++; 
	// 打印当前主设备上空闲和总的逻辑块数.
	Log(LOG_INFO_TYPE, "<<<<< %d/%d free blocks >>>>>\n\r", free, p->s_nzones);
	// 在显示过设备上空闲逻辑块数/逻辑块总数之后. 我们再统计设备上空闲 i 节点数. 
	// 首先令 i 等于超级块中表明的设备上 i 节点总数 + 1. 加 1 是将 0 节点也统计进去. 
	// 然后根据 i 节点位图中相应位的占用情况计算出空闲 i 节点数. 最后再显示设备上可用空闲 i 节点数和 i 节点总数.
	free = 0;
	i = p->s_ninodes + 1;
	while (--i >= 0)
		if (!set_bit(i & 8191, p->s_imap[i >> 13]->b_data))
			free++;
	// 打印当前主设备上 i 节点空闲和总的数量.
	Log(LOG_INFO_TYPE, "<<<<< %d/%d free inodes >>>>>\n\r", free, p->s_ninodes);
}