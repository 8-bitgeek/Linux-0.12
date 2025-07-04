/*
 *  linux/kernel/blk_dev/ll_rw.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * This handles all read/write requests to block devices
 */
#include <errno.h>
#include <linux/sched.h>					// 调试程序头文件, 定义了任务结构 task_struct, 任务 0 数据等.
#include <linux/kernel.h>
#include <asm/system.h>						// 系统头文件. 定义了设置或修改描述符/中断门等的嵌入式汇编宏.

#include "blk.h"							// 块设备头文件. 定义请求数据结构, 块设备数据结构和宏等信息.

/*
 * The request-struct contains all necessary data
 * to load a nr of sectors into memory
 */
/*
 * 请求结构中含有加载 nr 个扇区数据到内存中去的所有必须的信息.
 */
// 磁盘等低级设备读写请求项数组队列. 
// 共有 NR_REQUEST = 32 个请求项(所有类型的设备共用这一个请求数组). (参见文件 kernel/blk_drv/blk.h)
struct request request[NR_REQUEST];

/*
 * used to wait on when there are no free requests
 * 是用于在设备读写请求数组没有空闲项时进程的临时等待处.
 */
struct task_struct * wait_for_request = NULL;

/* blk_dev_struct is:
 *		do_request-address
 *		next-request
 */
// 块设备数组. 该数组使用主设备号作为索引. 实际内容将在各块设备驱动程序初始化时填入.
// 例如, 硬盘驱动程序初始化时(hd.c), 第一条语句即用于设备 blk_dev[3] 的内容.
// blk_dev_struct 在 kernel/blk_drv/blk.h 中, 结构体中有两个字段: request_fn, request.
struct blk_dev_struct blk_dev[NR_BLK_DEV] = {
	{ NULL, NULL },		/* no_dev */		// 0 - 无设备
	{ NULL, NULL },		/* dev mem */		// 1 - 内存
	{ NULL, NULL },		/* dev fd */		// 2 - 软驱设备
	{ NULL, NULL },		/* dev hd */		// 3 - 硬盘设备
	{ NULL, NULL },		/* dev ttyx */		// 4 - ttyx 设备
	{ NULL, NULL },		/* dev tty */		// 5 - tty 设备
	{ NULL, NULL }		/* dev lp */		// 6 - lp 打印机设备
};

/*
 * blk_size contains the size of all block-devices:
 *
 * blk_size[MAJOR][MINOR]
 *
 * if (!blk_size[MAJOR]) then no minor size checking is done.
 */
/*
 * blk_size 数组含有所有块设备的大小(块总数):
 * blk_size[MAJOR][MINOR]
 * 如果(!blk_size[MAJOR]), 则不必检测子设备的块总数.
 */
// 设备数据块总数指针数组. 每个指针项指向指定主设备号的总块数组. 
// 该总块数数组每一项对应子设备号确定的一个子设备上所拥有的数据总数(1 块大小 = 1KB).
int * blk_size[NR_BLK_DEV] = {NULL, NULL, }; 			// 这个数组里存放着各个块设备的数据块总数的指针.

// 锁定指定缓冲块.
// 如果指定的缓冲块已经被其他任务锁定, 则使自己睡眠(不可中断的等待), 直到被执行解锁缓冲块的任务明确地唤醒.
static inline void lock_buffer(struct buffer_head * bh) {
	cli();							// 关中断(屏蔽硬件中断).
	while (bh->b_lock) {			// 如果缓冲区已被锁定则睡眠, 直到缓冲区解锁.
		sleep_on(&bh->b_wait);
	}
	bh->b_lock = 1;					// 立刻锁定缓冲区.
	sti();							// 开中断.
}

// 释放(解锁)锁定的缓冲区.
// 该函数与 blk.h 文件中的同名函数完全一样.
static inline void unlock_buffer(struct buffer_head * bh)
{
	if (!bh->b_lock) {				// 如果该缓冲区没有被锁定, 则打印出错信息.
		printk("ll_rw_block.c: buffer not locked\n\r");
	}
	bh->b_lock = 0;					// 清锁定标志.
	wake_up(&bh->b_wait);			// 唤醒等待该缓冲区的任务.
}

/*
 * add-request adds a request to the linked list.
 * It disables interrupts so that it can muck with the
 * request-lists in peace.
 *
 * Note that swapping requests always go before other requests,
 * and are done in the order they appear.
 */
/*
 * add-request() 向链表中加入一项请求项. 它会关闭中断, 这样就能安全地处理请求链表了.
 *
 * 注意, 交换请求总是在其他请求之前操作, 并且以它们出现的顺序完成.
 */
// 向请求链表中加入新的请求项.
// 参数 dev 是指定块设备结构指针, 该结构中有该设备的请求处理函数指针和请求项链表指针;
// 本函数把已经设置好的请求项 req 添加到指定设备的请求项链表中. 
// 如果该设备在当前请求项指针为空, 则可以设置 req 为当前请求项并立刻调用设备请求处理函数. 
// 否则就把 req 请求项插入到该请求项链表中.
static void add_request(struct blk_dev_struct * dev, struct request * req) {
	// 首先对参数提供的请求项的指针和标志作初始设置. 置空请求项中的下一请求项指针, 关中断并清除请求项相关缓冲区脏标志.
	struct request * tmp;

	req->next = NULL;
	cli();								// 关中断(可以屏蔽硬件中断, 比如硬盘中断等).
	if (req->bh) {
		req->bh->b_dirt = 0;			// 清除缓冲区 "脏" 标志.
	}
	// 然后查看指定设备当前是否有请求项, 即查看设备是否正忙. 
	// 如果指定设备 dev 当前请求项(current_request)字段为空, 则表示目前该设备没有请求项, 
	// 本次是第 1 个请求项, 也是唯一的一个. 因此可将块设备的当前请求指针直接指向该请求项, 并立刻执行相应设备的请求处理函数.
	if (!(tmp = dev->current_request)) { // 当前该设备的请求队列为空.
		dev->current_request = req; 	// 直接设置为该设备的当前请求项.
		sti();							// 开中断.
		// **此时也不是同步等待 I/O 完成, 而是向硬盘提交了任务后立即返回, 也是等待硬盘中断通知请求完成.**
		(dev->request_fn)();			// 执行请求处理函数, 对于硬盘是 do_hd_request() (kernel/blk_drv/hd.c).
		return; 						// 读请求已将块设备中的数据读入高速缓冲区(buffer_head->data),
	} 									// 或写请求已将高速缓冲区中的数据写入块设备.
	// 如果目前该设备已经有请求在处理, 则首先利用电梯算法搜索最佳插入位置, 然后将当前请求项插入到请求队列中. 
	// 在搜索过程中, 如果要插入的请求项的缓冲块头指针空, 即没有缓冲块, 那么就需要找一个项, 其已经有可用的缓冲块. 
	// 因此若当前插入位置(tmp 之后)处的空闲项缓冲块头指针不空, 就选择这个位置, 于是退出循环并把请求项插入此处. 
	// 最后开中断并退出函数. 电梯算法的作用是让磁盘磁头的移动距离最小, 从而改善(减少)硬盘访问时间.
	// 下面 for 循环中 if 语句用于把 req 所指请求项与请求队列(链表)中已有的请求项作比较, 
	// 找出 req 插入该队列的正确位置顺序, 然后中断循环, 并把 req 插入到该队列正确位置处.
	for (; tmp->next; tmp = tmp->next) { 		// tmp 开始时指向当前正在处理的请求.
		if (!req->bh) 							// 如果新的请求项没有缓冲块.
			if (tmp->next->bh) {
				break;
			} else {
				continue;
			}
		if ((IN_ORDER(tmp, req) || !IN_ORDER(tmp, tmp->next)) && IN_ORDER(req, tmp->next)) {
			break;
		}
	}
	req->next = tmp->next;
	tmp->next = req;
	sti();
}

// 创建请求项并插入设备的请求队列中.
// 参数 major 是主设备号; rw 是指定命令; bh 是存放数据的缓冲区头指针.
static void make_request(int major, int rw, struct buffer_head * bh) {
	struct request * req;
	int rw_ahead;

	/* WRITEA/READA is special case - it is not really needed, so if the */
	/* buffer is locked, we just forget about it, else it's a normal read. */
	/* WRITEA/READA 是一种特殊情况 - 它们并非必要, 所以如果缓冲区已经上锁, 我们就忽略这个请求, 
	   否则它只是一个一般的读操作. */
	// 这里 'READ' 和 'WRITE' 后面的 'A' 字符代表英文单词 Ahead, 表示预读/写数据块的意思.
	// 该函数首先对命令 READA/WRITEA 的情况进行一些处理. 对于这两个命令, 当指定的缓冲区正在使用而已被上锁时, 
	// 就放弃预读/写请求, 否则就作为普通 READ/WRITE 命令进行操作. 
	// 另外, 如果参数给出的命令既不是 READ 也不是 WRITE, 则表示内核程序有错, 显示出错信息并停机. 
	// 注意, 在修改命令之前这里已为参数是否为预读/写命令设置了标志 rw_ahead.
	if (rw_ahead = (rw == READA || rw == WRITEA)) { // 如果该请求是预读或预写, 则 rw_ahead = 1, 同时进入条件表达式.
		if (bh->b_lock) {							// 缓冲块已经锁定的情况下忽略 READA/WRITEA 请求.
			return;
		}
		if (rw == READA) {							// 未锁定的情况是转换为普通的读写请求.
			rw = READ;
		} else {
			rw = WRITE;
		}
	}
	if (rw != READ && rw != WRITE) {
		panic("Bad block dev command, must be R/W/RA/WA");
	}
	lock_buffer(bh);                				// 因为要修改(写)缓冲块, 所以要锁定缓冲块.
	// 如果是 WRITE 操作并且缓冲块未修改, 或是 READ 操作并且缓冲块已更新, 则直接返回.
	if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) {
		unlock_buffer(bh);
		return;
	}
	/* we don't allow the write-requests to fill up the queue completely:
	 * we want some room for reads: they take precedence. The last third
	 * of the requests are only for reads.
	 */
	/*
	 * 我们不能让队列中全都是写请求项: 我们需要为读请求保留一些空间: 读操作是优先的. 
	 * 请求队列的后三分之一空间仅用于读请求项.
	 */
	// 生成并添加读/写请求项. 首先我们需要在请求数组中寻找到一个空闲项(槽)来存放新请求项. 
	// 搜索过程从请求数组末端开始, 根据上述要求, 对于读命令请求, 我们直接从队列末尾开始搜索, 
	// 而对于写请求就只能从队列 2/3 处向队列头处搜索空项填入. 
	// 于是我们开始从后向前搜索, 当请求结构 request 的设备字段 dev 值 = -1 时, 表示该项未被占用(空闲). 
	// 如果没有一项是空闲的(此时请求项数组指针已经搜索越过头部), 则查看此次请求是否是提前读/写(READA 或 WRITEA), 
	// 如果是则放弃此次请求操作, 否则让本次请求操作先睡眠(以等待请求队列腾出空闲项), 过一会儿再来搜索请求队列.
repeat:
	if (rw == READ) {
		req = request + NR_REQUEST;						// 对于读请求, 将指针指向请求队列尾部(最后一个请求槽的尾部).
	} else {
		req = request + ((NR_REQUEST * 2) / 3);			// 对于写请求, 指针指向队列 2/3 处.
	}
	/* find an empty request */
	/* 搜索一个空请求项 */
	while (--req >= request) {							// 还没查找完(没超过数组头部)时继续查找.
		if (req->dev < 0) { 							// 找到空闲项?
			break;
		}
	}
	/* if none found, sleep on new requests: check for rw_ahead */
	/* 如果没有找到空闲项, 则让该次请求操作睡眠: 需检查是否提前读/写 */
	if (req < request) {								// 如果已搜索到头(表明请求队列无空闲项).
		if (rw_ahead) {									// 则若是提前读/写请求, 则退出.
			unlock_buffer(bh); 							// 解锁缓冲块头, 以使得其它任务可以使用该缓冲块头.
			return;
		}
		sleep_on(&wait_for_request);					// 否则就睡眠, 过会再查看请求队列.
		goto repeat;
	}
	/* fill up the request-info, and add it to the queue */
	/* 向空闲请求项中填写请求信息, 并将其加入队列中 */
	// 已找到一个空闲请求项. 设置好新的请求项后就调用 add_request() 把它添加到请求队列中并退出. 
	// (请求结构参见 blk_drv/blk.h). req->sector 是读写操作的起始扇区号, req->buffer 是请求项存放数据的缓冲区.
	req->dev = bh->b_dev;								// 设备号.
	req->cmd = rw;										// 命令(READ/WRITE).
	req->errors = 0;									// 操作时产生的错误次数.
	// 设备内的相对起始扇区, 比如 hd1 的起始扇区是 200, sector 是 100, 则真正要读的扇区是 200 + 100 = 300.
	req->sector = bh->b_blocknr << 1;					// 块号转换成扇区号(1 块 = 2 扇区).
	req->nr_sectors = 2;								// 本请求项需要读写的扇区数.
	req->buffer = bh->b_data;							// 请求项缓冲区指向需要读写的缓存头的数据缓冲区.
	req->waiting = NULL;								// 等待该请求完成的任务.
	req->bh = bh;										// 缓冲块头指针.
	req->next = NULL;									// 指向下一请求项.
	add_request(major + blk_dev, req);					// 将请求项加入队列中(major + blk_dev ==> blk_dev[major], req).
}

// 低级页面读写函数(Low Level Read Write Page).
// 以页面(4K)为单位访问设备数据, 即每次读/写 8 个扇区. 参见下面 ll_rw_blk() 函数. 
// page - 要读取的页面号; buffer - 要缓存到的缓冲区指针.
void ll_rw_page(int rw, int dev, int page, char * buffer) {
	struct request * req;
	unsigned int major = MAJOR(dev);

	// 首先对函数参数的合法性进行检测. 如果设备主设备号不存在或者该设备的请求操作函数不存在, 则显示出错信息, 并返回. 
	// 如果参数给出的命令既不是 READ 也不是 WRITE, 则表示内核程序有错, 显示出错信息并停机.
	if (major >= NR_BLK_DEV || !(blk_dev[major].request_fn)) {
		printk("Trying to read nonexistent block-device\n\r");
		return;
	}
	if (rw != READ && rw != WRITE) {
		panic("Bad block dev command, must be R/W!");
	}
	// 在参数检测操作完成后, 我们现在需要为本次操作建立请求项. 
	// 首先我们需要在请求数组中寻找到一个空闲项(槽)来存放新请求项. 搜索过程从请求数组末端开始. 
	// 于是我们开始从后向前搜索, 当请求结构 request 的设备字段值 < 0 时, 表示该项未被占用(空闲). 
	// 如果没有一项是空闲的(此时请求项数组指针已经搜索越过头部), 
	// 则让本次请求操作先睡眠(以等待请求队列腾出空项), 过一会再来搜索请求队列.
repeat:
	req = request + NR_REQUEST;							// 将指针指向队列尾部.
	while (--req >= request) {
		if (req->dev < 0) {								// 找到空闲的请求项. 
			break;
		}
	}
	if (req < request) { 								// 低于数组起始地址则表示没有找到空闲的请求项.
		sleep_on(&wait_for_request);					// 睡眠, 过会再查看请求队列.
		goto repeat;
	}
	/* fill up the request-info, and add it to the queue */
	/* 向空闲请求项中填写请求信息, 并将其加入队列中 */
	// OK, 程序执行到这里表示已找到一个空闲请求项. 
	// 于是我们设置好新请求项, 把当前进程置为不可中断睡眠状态后, 就去调用 add_request() 把它添加到请求队列中,
	// 然后直接调用调度函数让当前进程睡眠等待页面从交换设备中读入. 
	// 这里不像 make_request() 函数那样直接退出函数而调用了 schedule(), 
	// 是因为 make_request() 函数仅读 2 个扇区数据. 而这里需要对交换设备读/写 8 个扇区, 需要花较长的时间. 
	// 因此当前进程肯定需要等待而睡眠. 因此这里直接就让进程去睡眠了, 省得在程序其他地方还要进行这些判断操作.
	req->dev = dev;										// 设备号.
	req->cmd = rw;										// 命令(READ/WRITE).
	req->errors = 0;									// 读写操作错误计数.
	req->sector = page << 3;							// 起始读写扇区. 一个页面 = 8 个扇区 = 8 * 512B = 4KB.
	req->nr_sectors = 8;								// 读写扇区数.
	req->buffer = buffer;								// 数据缓冲区.
	req->waiting = current;								// 当前进程进入该请求等待队列.
	req->bh = NULL;										// 无缓冲块头指针(不用高速缓冲区).
	req->next = NULL;									// 下一个请求项指针.
	current->state = TASK_UNINTERRUPTIBLE;				// 置为不可中断状态.
	add_request(major + blk_dev, req);					// 将请求项加入队列中.
	// 当前进程需要读取 8 个扇区的数据因此需要睡眠, 因此调用调度程序选择进程运行.
	schedule();
}

// 低级数据块读写函数(Low Level Read Write Block).
// 该函数是块设备驱动程序与系统其他部分的接口函数. 通常在 fs/buffer.c 程序中被调用.
// 主要功能是创建块设备读写请求项并插入到指定块设备请求队列. 
// 实际的读写操作则是由设备的 request_fn() 函数完成. 对于硬盘操作, 该函数是 do_hd_request(); 
// 对于软盘操作该函数是 do_fd_request(); 对于虚拟盘则是 do_rd_request(). 
// 另外, 在调用该函数之前, 调用者需要首先把要读/写的块设备信息保存在缓冲块头结构(buffer_head)中, 如设备号, 块号.
// 参数: rw - READ, READA, WRITE 或 WRITEA 是命令; bh - 数据缓冲块头指针.
void ll_rw_block(int rw, struct buffer_head * bh) {
	unsigned int major;									// 主设备号(对于硬盘是 3).

	// 如果请求的块设备主设备号不对或者该块设备的请求操作函数不存在, 则显示出错信息, 并返回. 否则创建请求项并插入请求队列.
	if ((major = MAJOR(bh->b_dev)) >= NR_BLK_DEV || !(blk_dev[major].request_fn)) {
		printk("Trying to read nonexistent block-device\n\r");
		return;
	}
	make_request(major, rw, bh); 						// 创建请求项并插入到请求队列中.
}

// 块设备初始化函数, 由初始化程序 main.c 调用.
// 初始化块设备请求数组, 将所有请求项置为空闲项(dev = -1). 有 32 项(NR_REQUEST = 32).
void blk_dev_init(void) {
	int i;

	for (i = 0; i < NR_REQUEST; i++) {
		request[i].dev = -1; 							// 请求对应的设备号, -1 表示没有请求.
		request[i].next = NULL;
	}
}