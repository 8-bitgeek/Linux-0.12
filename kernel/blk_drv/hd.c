/*
 *  linux/kernel/hd.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This is the low-level hd interrupt support. It traverses the
 * request-list, using interrupts to jump between functions. As
 * all the functions are called within interrupts, we may not
 * sleep. Special care is recommended.
 *
 *  modified by Drew Eckhardt to check nr of hd's from the CMOS.
 */
/*
 * 本程序是底层硬盘中断辅助程序. 主要用于扫描请求项队列, 使用中断在函数之间跳转. 
 * 由于所有函数都是在中断里调用的, 所以这些函数不可以睡眠. 请特别注意!
 *
 * 由 Drew Eckhardt 修改, 利用 CMOS 信息检测硬盘数.
 */
#include <linux/config.h>							// 内核配置头文件, 定义键盘语言和硬盘类型(HD_TYPE)选项.
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>							// 块设备头文件. 定义请求数据结构, 块设备数据结构和宏等信息.
#include <linux/hdreg.h>
#include <linux/mm.h>
#include <asm/system.h>
#include <asm/io.h>
//#include <asm/segment.h>

// 定义硬盘主设备号符号常数. 在驱动程序中, 主设备号必须在包含 blk.h 文件之前被定义.
// 因为 kernel/blk_drv/blk.h 文件中要用到这个符号常数来确定一些列其他相关符号常数和宏.
#define MAJOR_NR 3									// 硬盘主设备号是 3
#include "blk.h"

// 读 CMOS 参数宏函数.
// 这段宏读取 CMOS 中硬盘信息. outb_p, inb_p 是 include/asm/io.h 中定义的端口输入输出宏. 
// 与 init/main.c 中读取 CMOS 时钟信息的宏完全一样.
#define CMOS_READ(addr) ({ \
	outb_p(0x80|addr, 0x70); \
	inb_p(0x71); \
})

/* Max read/write errors/sector */
/* 每扇区读/写操作允许的最多出错次数 */
#define MAX_ERRORS	7							// 读/写一个扇区时允许的最多出错次数.
#define MAX_HD		2							// 系统支持的最多硬盘数.

// 重新校正处理函数.
// 复位操作时在硬盘中断处理程序中调用的重新校正函数
static void recal_intr(void);
// 读写硬盘失败处理调用函数
// 结束本次请求项处理或者设置复位标志要求执行复位硬盘控制器操作后再重试.
static void bad_rw_intr(void);

// 重新校正标志. 当设置了该标志, 程序中会调用 recal_intr() 以将磁头移动到 0 柱面.
static int recalibrate = 0;
// 复位标志. 当发生读写错误时会设置该标志并调用相关复位函数, 以复位硬盘和控制器.
static int reset = 0;

/*
 *  This struct defines the HD's and their types.
 */
/* 下面结构定义了硬盘参数及类型 */
// 硬盘信息结构(Harddisk information struct).
// 各字段分别是磁头数, 每磁道扇区数, 柱面数, 写前预补偿柱面号, 磁头着陆区柱面号, 控制字节.
struct hd_i_struct {
	int head;						// 磁头数
	int sect;						// 每磁道扇区数
	int cyl;						// 磁道(柱面)数
	int wpcom;						// 写前预补偿磁道(柱面)号
	int lzone;						// 磁头着陆区磁道(柱面)号
	int ctl;						// 控制字节
};

// 如果已经在 include/linux/config.h 配置文件中定义了符号常数 HD_TYPE, 
// 就取其中定义好的参数作为硬盘信息数组 hd_info[] 中的数据. 否则先默认都设为 0 值, 在 setup() 函数中会重新进行设置.
#ifdef HD_TYPE
struct hd_i_struct hd_info[] = { HD_TYPE };							// 硬盘信息数组.
#define NR_HD ((sizeof (hd_info)) / (sizeof (struct hd_i_struct)))	// 计算硬盘个数.
#else
struct hd_i_struct hd_info[] = { {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0} };
static int NR_HD = 0;
#endif

// 定义硬盘分区(每个硬盘最多有 4 个分区)结构. 保存每个分区从硬盘 0 开始算起的物理起始扇区号和分区扇区总数. 
// 其中 5 的倍数处的项(例如 hd[0] 和 hd[5] 等)代表整个硬盘的参数.
static struct hd_struct {
	long start_sect;		// 分区起始(绝对)扇区. (扇区 0 一般用于主引导扇区, 所以 start_sect 一般最小是 1)
	long nr_sects;			// 该分区拥有的扇区数.
} hd[5 * MAX_HD] = {{0, 0}, }; 

// 硬盘每个分区数据块总数(扇区数 / 2)数组.
static int hd_sizes[5 * MAX_HD] = {0, };

// 读端口嵌入汇编宏. 读端口 port, 共读 nr 字, 保存在 buf 中.
#define port_read(port, buf, nr) \
__asm__("cld; rep; insw" : : "d" (port), "D" (buf), "c" (nr))

// 写端口嵌入汇编宏. 写端口 port, 共写 nr 字, 从 buf 中取数据.
#define port_write(port, buf, nr) \
__asm__("cld; rep; outsw" : : "d" (port), "S" (buf), "c" (nr))

extern void hd_interrupt(void);		// 硬盘中断过程(sys_call.s)
extern void rd_load(void);			// 虚拟盘创建加载函数(ramdik.c)

/* This may be used only once, enforced by 'static int callable' */
/* 下面该函数只在初始化时被调用一次. 用静态变量 callable 作为可调用标志. */
// 系统设备函数.
// 函数参数 BIOS 是由初始化程序 init/main.c 中 init 子程序设置为指向硬盘参数表结构的指针.
// 该硬盘参数表结构包含 2 个硬盘参数表的(共 32 字节), 是从内存 0x90080 处复制而来. 
// 0x90080 处的硬盘参数表是由 setup.s 程序利用 ROM BIOS 功能取得. 
// 本函数主要功能是读取 CMOS 和硬盘参数表信息, 用于设置硬盘分区结构 hd, 并尝试加载 RAM 虚拟盘和根文件系统.
int sys_setup(void * BIOS) {
	static int callable = 1;	// 限制本函数只能被调用 1 次的标志.
	int i, drive;
	unsigned char cmos_disks;
	struct partition * p; 		// 硬盘分区表指针.
	struct buffer_head * bh;

	// 首先设置 callable 标志, 使得本函数只能被调用 1 次. 然后设置硬盘信息数组 hd_info[]. 
	// 如果在 include/linux/config.h 文件已定义了符号常数 HD_TYPE, 则 hd_info[] 已经设置好了.
	// 否则就需要读取硬盘参数表(drive_info), 这个数据由 boot/setup.s 读取到 0x90080 处, 并由 main() 方法复制到 drive_info 里.
	if (!callable) {
		return -1;
	}
	callable = 0;
#ifndef HD_TYPE															// 如果没有定义 HD_TYPE, 则读取.
	for (drive = 0; drive < 2; drive++) {
		hd_info[drive].cyl = *(unsigned short *)BIOS;					// 磁道(柱面)数.
		hd_info[drive].head = *(unsigned char *)(2 + BIOS);				// 磁头数.
		hd_info[drive].wpcom = *(unsigned short *)(5 + BIOS);			// 写前预补偿柱面号.
		hd_info[drive].ctl = *(unsigned char *)(8 + BIOS);				// 控制字节.
		hd_info[drive].lzone = *(unsigned short *)(12 + BIOS);			// 磁头着陆区柱面号.
		hd_info[drive].sect = *(unsigned char *)(14 + BIOS);			// 每磁道扇区数.
		BIOS += 16;														// 每个硬盘参数表长 16 字节, 这里 BIOS 指向下一表.
	}
	// setup.s 程序在取 BIOS 硬盘参数表信息时, 如果系统中只有 1 个硬盘, 就会将对应第 2 硬盘的 16 字节全部清零. 
	// 因此这里只要判断第 2 个硬盘磁道(柱面)数是否为 0 就可以知道是否有第 2 个硬盘了.
	if (hd_info[1].cyl) {
		NR_HD = 2;														// 硬盘数置为 2.
	} else {
		NR_HD = 1;
	}
#endif
	// 到这里, 硬盘信息数组 hd_info[] 已经设置好, 并且确定了系统含有的硬盘数 NR_HD. 现在开始设置硬盘分区结构数组 hd[]. 
	// 该数组的项 0 和项 5 分别表示两个硬盘的整体参数, 而项 1-4 和 6-9 分别表示两个硬盘的 4 个分区参数. 
	// 因此这里仅设置硬盘整体信息的两项(项 0 和 5).
	for (i = 0; i < NR_HD; i++) {
		hd[i * 5].start_sect = 0;													// 硬盘起始扇区号.
		hd[i * 5].nr_sects = hd_info[i].head * hd_info[i].cyl * hd_info[i].sect;	// 硬盘总扇区数 = 磁头数 * 磁道(柱面)数 * 磁道扇区数.
	}

	/*
		We query CMOS about hard disks : it could be that
		we have a SCSI/ESDI/etc controller that is BIOS
		compatable with ST-506, and thus showing up in our
		BIOS table, but not register compatable, and therefore
		not present in CMOS.

		Furthurmore, we will assume that our ST-506 drives
		<if any> are the primary drives in the system, and
		the ones reflected as drive 1 or 2.

		The first drive is stored in the high nibble of CMOS
		byte 0x12, the second in the low nibble.  This will be
		either a 4 bit drive type or 0xf indicating use byte 0x19
		for an 8 bit type, drive 1, 0x1a for drive 2 in CMOS.

		Needless to say, a non-zero value means we have
		an AT controller hard disk for that drive.
	*/
	/*
		我们对 CMOS 有关硬盘的信息有些怀疑: 可能会出现这样的情况, 我们有一块 SCSI/ESDI 等的控制器, 
		它是以 ST-506 方式与 BIOS 兼容的, 因而会出现在我们的 BIOS 参数表中, 但又不是寄存器兼容的, 
		因此这些参数在 CMOS 中又不存在.
		另外, 我们假设 ST-506 驱动器(如果有的话)是系统中的基本驱动器, 标号为驱动器 1 或 2.
		第 1 个驱动参数存放在 CMOS 字节 0x12 的高半字节, 第 2 个存放在低半字节中. 
		该 4 位字节信息可以是驱动器类型, 也可能仅是 0xf.
		0xf 表示使用 CMOS 中 0x19 字节作为驱动器 1 的 8 位类型字节, 使用 CMOS 中 0x1A 字节作为驱动器 2 的类型字节.
		总之, 一个非零值意味着硬盘是一个 AT 控制器兼容硬盘.
	*/
	// 根据上述原理, 下面代码用来检测硬盘到底是不是 AT 控制器兼容的. 
	// 这里从 CMOS 偏移地址 0x12 处读出硬盘类型字节. 
	// 如果低半字节值(存放着第 2 个硬盘类型值)不为 0, 则表示系统有两硬盘, 否则表示系统只有 1 个硬盘. 
	// 如果 0x12 处读出的值为 0, 则表示系统中没有 AT 兼容硬盘.
	if ((cmos_disks = CMOS_READ(0x12)) & 0xf0) {
		if (cmos_disks & 0x0f) {
			NR_HD = 2;
		} else {
			NR_HD = 1;
		}
	} else {
		NR_HD = 0;
	}
	// 若 NR_HD = 0, 则两个硬盘都不是 AT 控制器兼容的, 两个硬盘数据结构全清零. 
	// 若 NR_HD = 1, 则将第 2 个硬盘的参数清零.
	for (i = NR_HD; i < 2; i++) {
		hd[i * 5].start_sect = 0;
		hd[i * 5].nr_sects = 0;
	}
	// 好, 到此为止我们已经真正确定了系统中所含的硬盘个数 NR_HD. 现在我们来读取每个硬盘上 0 号扇区中的分区表信息, 
	// 用来设置分区结构数组 hd[] 中硬盘各分区的信息. 首先利用读函数 bread() 读取硬盘第 0 号数据块(fs/buffer.c), 
	// 第 1 个参数(0x300, 0x305)分别是两个硬盘的设备号, 第 2 个参数(0)是所需读取的块号. 
	// 若读操作成功, 则数据会被存放在缓冲块 bh 的数据区中.
	// 若缓冲块头指针 bh 为 0, 则说明读操作失败, 则显示出错信息并停机. 
	// 否则我们根据硬盘第 1(0) 个扇区最后两个字节应该是 0xAA55 来判断扇区中数据的有效性, 
	// 从而可以知道扇区中位于偏移 0x1BE 开始处的分区表是否有效. 
	// 若有效则将硬盘分区表信息放入硬盘分区结构数组 hd[] 中. 最后释放 bh 缓冲区.
	for (drive = 0; drive < NR_HD; drive++) {
		// 0x300 表示整个第 1 个硬盘, 对应的设备文件是 /dev/hd0, 
		// 0x301 表示第 1 个硬盘的第 1 个分区, 对应的设备文件是 /dev/hd1, 
		// 0x305 表示整个第 2 个硬盘. 对应的设备文件是 /dev/hd5.
		if (!(bh = bread(0x300 + drive * 5, 0))) {			// 0x300, 0x305 是设备号. 参见 p218 表 6-1.
			printk("Unable to read partition table of drive %d\n\r", drive);
			panic("");
		}
		if (bh->b_data[510] != 0x55 || (unsigned char) bh->b_data[511] != 0xAA) {	// 判断硬盘标志 0xAA55(有效引导扇区标志). 
			printk("Bad partition table on drive %d\n\r", drive);
			panic("");
		}
		p = 0x1BE + (void *)bh->b_data;	 				// p 指向硬盘分区表, 位于第 1(0) 扇区 0x1BE 开始处.
		for (i = 1; i < 5; i++, p++) { 					// 保存每个分区的起始扇区及拥有的扇区数.(每个硬盘最多 4 个分区)
			// hd[1] 表示第一个硬盘的第一个分区; hd[6] 表示第二个硬盘的第一个分区.
			hd[i + 5 * drive].start_sect = p->start_sect;
			// 一个硬盘的第 0 个扇区一般是主引导扇区. 所以 start_sect == 1 或更大的值.
			hd[i + 5 * drive].nr_sects = p->nr_sects;
		}
		brelse(bh);										// 释放为了存放硬盘数据块而申请的缓冲区.
    }
	// 现在再对每个分区中的数据块总数进行统计, 并保存在硬盘各分区总数据块数组 hd_sizes[] 中. 
	// 然后让设备数据块总数指针数组的本设备项指向该数组.
	for (i = 0; i < 5 * MAX_HD; i++) {
		if (hd[i].nr_sects != 0) {
			Log(LOG_INFO_TYPE, "<<<<< HD Partition[%d] Info: start_sect = %d, nr_sects = %d, end_sect = %d >>>>>\n", 
				i, hd[i].start_sect, hd[i].nr_sects, hd[i].start_sect + hd[i].nr_sects);
		}
		hd_sizes[i] = hd[i].nr_sects >> 1 ;
	}
	blk_size[MAJOR_NR] = hd_sizes;
	// 如果确实有硬盘存在并且读入其分区表, 则显示 "分区表正常" 信息. 
	// 然后尝试在系统内存虚拟盘中加载启动盘中包含的根文件系统映像(kernel/blk_drv/ramdisk.c).
	// 即在系统设置有虚拟盘的情况下判断启动盘上是否还含有根文件系统的映像数据. 
	// 如果有(此时该启动盘称为集成盘)则尝试把该映像加载并存放到虚拟盘中, 
	// 然后把此时的根文件系统设备号 ROOT_DEV 修改成虚拟盘的设备号. 
	// 接着再对交换设备进行初始化. 最后安装根文件系统.
	if (NR_HD) {
		Log(LOG_INFO_TYPE, "<<<<< Partition table%s ok. >>>>>\n\r", (NR_HD > 1) ? "s" : "");
	}
	for (i = 0; i < NR_HD; i++) {
		Log(LOG_INFO_TYPE, "<<<<< HD[%d] Info: cyl = %d, head = %d, sect = %d, ctl = %x >>>>>\n", i, hd_info[i].cyl, hd_info[i].head, hd_info[i].sect, hd_info[i].ctl);
	}
	rd_load();						// 尝试在虚拟盘中加载根文件系统. (kernel/blk_drv/ramdisk.c)
	// 初始化交换设备使用位图, 如果存在交换设备, 则在主内存中申请一页物理内存(4KB)生成交换内存位图信息 swap_bitmap. 
	init_swapping();				// (mm/swap.c)
	mount_root();					// 安装根文件系统. (fs/super.c)
	return (0);
}

// 判断并循环等待硬盘控制器就绪.
// 读硬盘控制器状态寄存器端口 HD_STATUS(0x1f7), 
// 循环检测其中的驱动器就绪位(位 6)是否被置位并且控制器忙位(位 7)是否被复位.
// 如果返回值 retries 为 0, 则表示等待控制器空闲的时间已经超时而发生错误, 
// 若返回值不为 0 则说明在等待(循环)时间期限内控制器回到空闲状态. OK!
// 实际上, 我们仅需检测状态寄存器忙位(位 7)是否为 1 来判断控制器是否处于忙状态, 
// 驱动器是否就绪(即位 6 是否为 1)与控制器的状态无关. 
// 因此我们可能把第 172 行语句改写成: "while(--retries && (inb_p(HD_STATUS)&0x80));" 
// 另外, 由于现在的 PC 速度都很快, 因此我们可以把等待的循环次数再加大一些, 例如再增加 10 倍.
static int controller_ready(void) {
	int retries = 100000;

	// while (--retries && (inb_p(HD_STATUS)&0xc0)!=0x40);
	while(--retries && (inb_p(HD_STATUS) & 0X80)) {
		// nothing.
	}
	return (retries);									// 返回等待循环次数.
}

// 检测硬盘执行命令后的状态. (win 表示温切斯特硬盘的缩写)
// 读取硬盘状态寄存器中的命令执行结果状态. 返回 0 表示正常; 1 表示出错. 
// 如果执行命令错, 则需要再读错误寄存器 HD_ERROR(0x1f1).
static int win_result(void) {
	int i = inb_p(HD_STATUS);							// 读取硬盘状态寄存器中的状态信息.

	if ((i & (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT)) == (READY_STAT | SEEK_STAT)) {
		return(0); 										/* ok */
	}
	if (i & 1) {
		i = inb(HD_ERROR);								// 若 ERR_STAT 置位, 则读取错误寄存器.
	}
	return (1);
}

// 向硬盘控制器发送命令块.
// 参数: drive - 硬盘号(0-1); nsect - 读写扇区数; sect - 起始扇区;
//     head - 磁头号; cyl - 柱面号; cmd - 命令码
//     intr_addr() - 硬盘中断处理中将调用的 C 处理函数指针.
// 该函数在硬盘控制器就绪之后, 先设置全局指针亦是 do_hd 为硬盘中断处理程序中将调用的 C 处理函数指针. 
// 然后发送硬盘控制字节和7字节的参数命令块. 硬盘中断处理程序的代码位于 kernel/sys_call.s 程序中.
// 关键字 `register` 定义 1 个寄存器变量 port. 该变量将被保存在 1 个寄存器中, 以便于快速访问.
// 如果想指定寄存器(如 eax), 则我们可以把该句写成 "register char __res asm("ax");"
static void hd_out(unsigned int drive, unsigned int nsect, unsigned int sect, unsigned int head, unsigned int cyl, unsigned int cmd, void (*intr_addr)(void)) {
	register int port;

	// 首先对参数进行有效性检查. 如果驱动器号大于 1(只能是 0, 1)或者磁头号大于 15, 则程序不支持, 停机. 
	// 否则就判断并循环等待驱动器就绪. 如果等待一段时间后仍未就绪则表示硬盘控制器出错, 也停机.
	if (drive > 1 || head > 15) {
		panic("Trying to write bad sector");
	}
	if (!controller_ready()) {
		panic("HD controller not ready");
	}
	// 接着设置硬盘中断发生时将调用的函数指针 do_hd(kernel/blk_drv/blk.h). 在向硬盘控制器发送参数和命令之前, 
	// 规定要先向控制器命令端口(0x3f6)发送指定硬盘的控制字节, 以建立相应的硬盘控制方式. 
	// 该控制字节即是硬盘信息结构数组中的 ctl 字节. 然后向控制器端口 0x1f1 - 0x1f7 发送 7 字节的参数命令块.
	SET_INTR(intr_addr);								// 设置 do_hd = intr_addr 在硬盘触发中断时被调用.
	outb_p(hd_info[drive].ctl, HD_CMD);					// 向控制寄存器输出控制字节
	port = HD_DATA;										// 置 dx 为数据寄存器端口(0x1f0)
	outb_p(hd_info[drive].wpcom >> 2, ++port);			// 参数: 写预补偿柱面号(需除 4)
	outb_p(nsect, ++port);								// 参数: 读/写扇区总数.
	outb_p(sect, ++port);								// 参数: 起始扇区.
	outb_p(cyl, ++port);								// 参数: 柱面号低 8 位.
	outb_p(cyl >> 8, ++port);							// 参数: 柱面号高 8 位.
	outb_p(0xA0 | (drive << 4) | head, ++port);			// 参数: 驱动器号 + 磁头号.
	outb(cmd, ++port);									// 命令: 硬盘控制命令.
}

// 等待硬盘就绪.
// 该函数循环等待主状态控制器忙标志复位. 若仅有就绪或寻道结束标志置位, 则表示就绪, 成功返回 0. 
// 若经过一段时间仍为忙, 则返回 1.
static int drive_busy(void) {
	unsigned int i;
	unsigned char c;

	// 循环读取控制器的主状态寄存器 HD_STATUS, 等待就绪标志位置位并且忙位复位. 然后检测其中忙位, 就绪位和寻道结束位. 
	// 若仅有就绪或寻道结束标志置位, 则表示硬盘就绪, 返回 0. 否则表示等待超时. 于是警告显示信息. 并返回 1.
	for (i = 0; i < 50000; i++) {
		c = inb_p(HD_STATUS);							// 取主控制器状态字节.
		c &= (BUSY_STAT | READY_STAT | SEEK_STAT);
		if (c == (READY_STAT | SEEK_STAT)) {
			return 0;
		}
	}
	printk("HD controller times out\n\r");				// 等待超时, 显示信息. 并返回 1.
	return(1);
}

// 诊断复位(重新校正)硬盘控制器.
// 首先向控制器寄存器端口(0x3f6)发送允许复位(4)控制字节. 然后循环操作等待一段时间让控制器执行复位操作. 
// 接着再向该端口发送正常的控制字节(不禁止重试, 重读)并等待硬盘就绪. 若等待硬盘就绪超时, 则显示警告信息. 
// 然后读取错误寄存器内容, 若其不等于 1(表示无错误)则显示硬盘控制器复位失败信息.
static void reset_controller(void) {
	int	i;

	outb(4, HD_CMD);									// 向控制寄存器端口发送复位控制字节.
	for(i = 0; i < 1000; i++) {							// 等待一段时间.
		nop();
	}
	outb(hd_info[0].ctl & 0x0f, HD_CMD);				// 发送正常控制字节(不禁止重试, 重读).
	if (drive_busy()) {
		printk("HD-controller still busy\n\r");
	}
	if ((i = inb(HD_ERROR)) != 1) {
		printk("HD-controller reset failed: %02x\n\r",i);
	}
}

// 硬盘复位操作.
// 首先复位(重新校正)硬盘控制器. 然后发送硬盘控制器命令 "建立驱动器参数". 在本命令引起的硬盘中断处理程序中又会调用本函数. 
// 此时该函数会根据执行该命令的结果判断是否要进行出错处理或是继续执行请求项处理操作.
static void reset_hd(void) {
	static int i;

	// 如果复位标志 reset 是置位的, 则把复位标志清零后, 执行复位硬盘控制在操作. 
	// 然后针对第 i 个硬盘向控制器发送 "建立驱动器参数" 命令.
	// 当控制器执行了该命令后, 又会发出硬盘中断信号. 此时本函数会被中断过程调用而再次执行. 
	// 由于 reset 已经标志复位, 因此会首先去执行 246 行开始的语句,
	// 判断命令执行是否正常. 若还是发生错误就会调用 bad_rw_intr() 函数以统计出错次数, 
	// 并根据次数确定是否在设置 reset 标志, 如果又设置了 reset 标志则跳转到 repeat 重新执行本函数. 
	// 若复位操作正常, 则针对下一个硬盘发送 "建立驱动器参数" 命令, 并作上述处理. 
	// 如果系统中 NR_HD 个硬盘都已经正常执行了发送的命令, 则再次 do_hd_request() 函数开始对请求项进行处理.
repeat:
	if (reset) {
		reset = 0;
		i = -1;											// 初始化当前硬盘号(静态变量).
		reset_controller();
	} else if (win_result()) {
		bad_rw_intr();
		if (reset) {
			goto repeat;
		}
	}
	i++;												// 处理下一个硬盘(第 1 个是 0).
	if (i < NR_HD) {
		hd_out(i, hd_info[i].sect, hd_info[i].sect,hd_info[i].head - 1, hd_info[i].cyl, WIN_SPECIFY, &reset_hd);
	} else {
		do_hd_request();								// 执行请求项处理.
	}
}

// 意外硬盘中断调用函数
// 发生意外硬盘中断时, 硬盘中断处理程序中调用的默认 C 处理函数. 在被调用函数指针为 NULL 时调用该函数. 
// 该函数在显示警告信息后设置复位标志 reset, 然后继续调用请求项函数 do_hd_request() 并在其中执行复位处理操作.
void unexpected_hd_interrupt(void) {
	printk("Unexpected HD interrupt\n\r");
	reset = 1;
	do_hd_request();
}

// 读写硬盘失败处理调用函数
// 如果读扇区时的出错次数大于或等于 7 次时, 则结束当前请求项并唤醒等待该请求的进程, 
// 而且对应缓冲区更新标志复位, 表示数据没有更新. 
// 如果读写一扇区时的出错次数已经大于 3 次, 则要求执行复位硬盘控制器操作(设置复位标志).
static void bad_rw_intr(void) {
	if (++CURRENT->errors >= MAX_ERRORS) {
		end_request(0);
	}
	if (CURRENT->errors > MAX_ERRORS / 2) {
		reset = 1;
	}
}

// 读操作中断调用函数.
// 在硬盘读命令执行完成后会产生硬盘中断信号, 并执行硬盘中断处理程序(hd_interrupt), 
// 此时在硬盘中断处理程序调用的 C 函数指针 do_hd 已经指向 read_intr(), 
// 因此会在一次读扇区操作完成(或出错)后就会执行该函数.
static void read_intr(void) {
	// 首先判断此次读请求操作是否出错. 若命令结束后控制器还处于忙状态, 或者命令执行错误, 则处理硬盘操作失败的问题, 
	// 接着再次请求硬盘作复位处理并执行其他请求项. 然后返回. 
	// 每次读操作出错都会对当前请求项作出错次数累计, 若出错次数不到最大允许出错次数一半, 
	// 则会先执行硬盘复位操作, 然后再执行本次请求项处理. 若出错次数已经大于等于最大允许出错次数 MAX_ERRORS(7次), 
	// 则结束本次请求项的处理而去处理队列中下一个请求项.
	if (win_result()) {									// 若控制器忙, 读写错或命令执行错, 则进行读写硬盘失败处理. win 表示温切斯特硬盘.
		bad_rw_intr();
		do_hd_request();								// 再次请求硬盘作相应(复位)处理.
		return;
	}
	// 如果读命令没有出错, 则从数据寄存器端口把 1 扇区的数据(512B)读到请求项的缓冲区中, 并且递减请求项所需读取的扇区数值. 
	// 再次设置 do_hd 指针指向 read_intr(), 因为硬盘中断处理程序每次都会将函数指针 do_hd 置空.
	port_read(HD_DATA, CURRENT->buffer, 256);			// 每次从硬盘中读取一个扇区的数据(512B)到缓冲块中.
	CURRENT->errors = 0;								// 清出错次数.
	CURRENT->buffer += 512;								// 数据缓冲区指针, 指向新的待读入数据缓冲区.
	CURRENT->sector++;									// 起始扇区号加 1.
	if (--CURRENT->nr_sectors) {						// 如果所需数据还没读完, 则再次设置硬盘中断调用函数为 read_intr().
		SET_INTR(&read_intr);
		return; 										// 直接返回等待下次硬盘中断时再次读取数据.
	}
	// 执行到此, 说明本请求项的全部数据已经读完, 则调用 end_request() 函数去处理请求项结束事宜. 
	// 最后再次调用 do_hd_request(), 去处理其他硬盘请求项. 执行其他硬盘请求操作.
	end_request(1);										// 数据已更新标志置位(1).
	do_hd_request();
}

// 写扇区中断调用函数
// 该函数将在硬盘写命令结束引发的硬盘中断过程中被调用. 函数功能与 read_intr() 类似. 
// 在写命令执行后会产生硬盘中断信号, 并执行硬盘中断处理程序, 
// 此时在硬盘中断处理程序中调用的 C 函数指针 do_hd 已经指向 write_intr(), 
// 因此会在一次写扇区操作完成(或出错)后就会执行该函数.
static void write_intr(void) {
	// 该函数首先判断此次写命令操作是否出错. 若命令结束后控制器还处于忙状态, 或者命令执行错误, 则处理硬盘操作失败问题, 
	// 接着再次请求硬盘作复位处理并执行其他请求项. 然后返回. 
	// 在 bad_rw_intr() 函数中, 每次操作出错都会对当前请求项作出错次数累计, 
	// 若出错次数不到最大允许出错次数的一半, 则会先执行硬盘复位操作, 然后再执行本次请求项处理. 
	// 若出错次数已经大于等于最大允许出错次数 MAX_ERRORS(7 次), 则结束本次请求项的处理而去处理队列中下一个请求项. 
	// do_hd_request() 中会根据当时具体的标志状态来判别是否需要先执行复位, 重新校正等操作, 然后再继续或处理下一个请求项.
	if (win_result()) {		// 如果硬盘控制器返回错误信息, 则首先进行硬盘读写失败处理, 再次请求硬盘作相应(复位)处理.
		bad_rw_intr();
		do_hd_request();
		return;
	}
	// 此时说明本次写一扇区操作成功, 因为将欲写扇区数减 1. 若其不为 0, 则说明还有扇区要写, 
	// 于是把当前请求起始扇区号 + 1, 并调整请求项数据缓冲区指针指向下一块欲写的数据. 
	// 然后再重置硬盘中断处理程序中调用的 C 函数指针 do_hd(指向本函数).
	// 接着向控制器数据端口写入 512 字节数据, 然后函数返回去等待控制器把些数据写入硬盘后产生的中断.
	if (--CURRENT->nr_sectors) {						// 若还有扇区要写, 则
		CURRENT->sector++;								// 当前请求起始扇区号 + 1,
		CURRENT->buffer += 512;							// 调整请求缓冲区指针,
		SET_INTR(&write_intr);							// do_hd 置函数指针为 write_intr().
		port_write(HD_DATA, CURRENT->buffer, 256);		// 向数据端口写 256 字.
		return;
	}
	// 若本次请求项的全部扇区数据已经写完, 则调用 end_request() 函数去处理请求项结束事宜. 
	// 最后再次调用 do_hd_requrest(), 去处理其他硬盘请求项. 执行其他硬盘请求操作.
	end_request(1);										// 处理请求结束事宜(已设置更新标志).
	do_hd_request();									// 执行其他硬盘请求操作.
}

// 硬盘重新校正(复位)中断调用函数.
// 该函数会在硬盘执行重新校正操作而引发的硬盘中断中被调用.
// 如果硬盘控制器返回错误信息, 则函数首先进行硬盘读写失败处理, 然后请求硬盘作相应(复位)处理. 
// 在 bad_rw_intr() 函数中, 每次操作出错都会对当前请求项作出错次数累计, 
// 若出错次数不到最大允许出错次数一半, 则会先执行硬盘复位操作, 然后再执行本次请求项处理.
// 若出错次数已经大于等于最大允许出错次数 MAX_ERRORS(7 次), 则结束本次请求项的处理而去处理队列中下一个请求项. 
// do_hd_request() 中根据当时具体的标志状态来判别是否需要先执行复位, 重新校正等操作, 然后再继续或处理下一请求项.
static void recal_intr(void) {
	if (win_result()) {									// 若返回出错, 则调用 bad_rw_intr().
		bad_rw_intr();
	}
	do_hd_request();
}

// 硬盘操作超时处理
// 本函数会在 do_timer() 中(kernel/sched.c)被调用. 
// 在向硬盘控制器发送了一个命令后, 若在经过了 hd_timeout 个系统滴答后控制器还没有发出一个硬盘中断信号, 
// 则说明控制器(或硬盘)操作超时. 
// 此时 do_timer() 就会调用本函数设置复位标志 reset 并调用 do_hd_request() 执行复位处理.
// 若在预定时间内(200 滴答)硬盘控制器发出了硬盘中断并开始执行硬盘中断处理程序, 
// 那么 hd_timeout 值就会在中断处理程序中被置 0. 此时 do_timer() 就会跳过本函数.
void hd_times_out(void) {
	// 如果当前并没有请求项要处理(设备请求项指针为 NULL), 则无超时可言, 直接返回. 否则先显示警告信息, 
	// 然后判断当前请求项执行过程中发生的出错次数是否已经大于设定值 MAX_ERRORS(7).
    // 如果是则以失败形式结束本次请求项的处理(不设置数据更新标志). 
	// 然后把中断过程中调用的 C 函数指针 do_hd 置空, 并设置复位标志 reset, 
	// 继而在请求项处理函数 do_hd_request() 中去执行复位操作.
	if (!CURRENT) {
		return;
	}
	printk("HD timeout");
	if (++CURRENT->errors >= MAX_ERRORS) {
		end_request(0);
	}
	SET_INTR(NULL);										// 令 do_hd = NULL, time_out = 200
	reset = 1;											// 设置复位标志.
	do_hd_request();
}

// 执行硬盘读写请求操作.
// 该函数根据设备当前请求项中的设备号和起始扇区号信息首先计算到对应硬盘上的柱面号, 当前磁道中扇区号, 磁头号数据, 
// 然后再根据请求项中的命令(READ/WRITE)对硬盘发送相应读/写命令若控制器复位标志或硬盘重新校正已被置位, 
// 那么首先会执行复位或重新校正操作.
// 若请求项此时是块设备的第 1 个(原来设备空闲), 则块设备当前请求项指针会直接指向该请求项(参见 ll_rw_blk.c), 
// 并会立刻调用本函数执行读写操作. 
// 否则在一个读写操作完成而引发的硬盘中断过程, 若还有请求项需要处理, 则也会在硬盘中断过程中调用本函数
void do_hd_request(void) {
	int i, r;
	unsigned int block, dev;
	unsigned int sec, head, cyl;
	unsigned int nsect;

	// 函数首先检测请求项的合法性. 若请求队列中已没有请求项则退出(参见 kernel/blk_drv/blk.h)
	// 然后取设备号中的子设备号以及设备当前请求项中的起始扇区号. 
	// 子设备号即对应硬盘上各分区(0 - 整个硬盘; 1 - 第一分区; 2 - 第二分区... 5 - 第二个硬盘, 6 - 第二个硬盘第一个分区...). 
	// 如果子设备号不存在或者起始扇区大于该分区扇区数 - 2, 则结束该请求项, 并跳转到标号 repeat 处(定义在 INIT_REQUEST 开始处).
	// 因为一次请求要读写一个缓冲块数据(2 个扇区, 即 1024 字节), 所以请求的扇区号不能大于分区中最后倒数第二个扇区号. 
	// 然后通过加上子设备号对应分区的起始扇区号, 就把需要读写的块对应到整个硬盘的绝对扇区号 block 上. 
	// 而子设备号除以 5 即可得到对应的硬盘号(0x305 / 5 ==> 5 / 5 = 1 ==> 第 1(从 0 开始)个硬盘).
	INIT_REQUEST; 									// 校验请求参数是否正确.
 	dev = MINOR(CURRENT->dev); 						// 取当前请求项的子设备号.
	block = CURRENT->sector;						// 当前请求的起始扇区号.
	if (dev >= 5 * NR_HD || block + 2 > hd[dev].nr_sects) { // 如果参数不对, 则结束请求.
		end_request(0);
		goto repeat;								// 该标号在 INIT_REQUEST(kernel/blk_drv/blk.h) 开始处.
	}
	block += hd[dev].start_sect; 					// 得到绝对扇区号(整个磁盘中的扇区号).
	dev /= 5;										// 此时 dev 代表硬盘号(硬盘 0 还是硬盘 1).
	// 然后根据绝对扇区号 block 和硬盘号 dev, 计算出对应硬盘中的磁道中扇区号(sec), 所在磁道(柱面)号(cyl)和磁头号(head).
	// 计算方法为: 初始时 eax 是扇区号 block, edx 中置 0. 
	// 			 divl 指令把 edx:eax 组成的扇区号除以每磁道扇区数(hd_info[dev].sect),
	// 			 所得整数商值在 eax 中, 余数在 edx 中. 
	// 			 其中 eax 中是到指定位置的对应总磁道数(所有磁头面) block, edx 中是当前磁道上的扇区号(sec). 
	__asm__("divl %4"                   \
	     	: "=a" (block), "=d" (sec) 	\
			: "0" (block), "1" (0), "r" (hd_info[dev].sect));
	// 代码初始时 eax 是上面计算出的对应总磁道数, edx 中置 0. 
	// divl 指令把 edx:eax 的对应总磁道数除以硬盘总磁头数(hd_info[dev].head),
	// 在 eax 中得到的整除值是柱面号(cyl), edx 得到的余数就是对应得当前磁头号(head).
	// 对应总磁道数 * 每磁道扇区数 + 当前磁道上的扇区号 = 绝对扇区号.
	// 总磁头数 * 柱面号 + 磁头号 = 对应总磁道数.
	__asm__("divl %4" 					\
	 		: "=a" (cyl), "=d" (head) 	\
			: "0" (block), "1" (0), "r" (hd_info[dev].head));
	sec++;											// 对计算所得当前磁道扇区号进行调整.
	nsect = CURRENT->nr_sectors;					// 要读/写的扇区数.
	// 此时我们得到了要读写的硬盘起始扇区 block 对应的硬盘上磁道号(cyl), 
	// 在当前磁道上的扇区号(sec), 磁头号(head)以及要读写的总扇区数(nsect). 
	// 查看是否硬盘控制器中是否有复位控制器状态和重新校正硬盘的标志, 通常在复位操作之后都需要重新校正硬盘磁头位置. 
	// 若这些标志已被置位, 则说明前面的硬盘操作可能出现了一些问题或者现在是系统第一次硬盘读写操作等情况. 
	// 于是我们就需要重新复位硬盘或控制器并重新校正硬盘. 如果此时复位标志 reset 是置位的, 则需要执行复位操作. 
	// 复位硬盘和控制器, 并置硬盘需要重新校正标志, 返回. 
	// reset_hd() 将首先向硬盘控制器发送复位(重新校正)命令, 然后发送硬盘控制命令 "建立驱动器参数".
	if (reset) {
		recalibrate = 1;							// 置位需要重新校正标志.
		reset_hd();
		return;
	}
	// 如果此时重新校正标志(recalibrate - 重新校准)是置位的, 则首先复位该标志, 然后向硬盘控制器发送重新校正命令. 
	// 该命令会执行寻道操作, 让处于任何地方的磁头移动到 0 柱面.
	if (recalibrate) {
		recalibrate = 0;
		hd_out(dev, hd_info[CURRENT_DEV].sect, 0, 0, 0, WIN_RESTORE, &recal_intr);
		return;
	}
	// 如果以上两个标志都没有置位, 那么我们就可以开始向硬盘控制器发送真正的数据读/写操作命令了. 
	// 如果当前请求是写扇区操作, 则发送命令, 循环读取状态寄存器信息并判断请求服务标志 DRQ_STAT 是否置位. 
	// DRQ_STAT 是硬盘状态寄存器的请求服务位, 表示驱动器已经准备好在主机和数据端口之间传输一个字或一个字节的数据.
	// 如果请求服务 DRQ 置位则退出循环. 若等到循环结束也没有置位, 则表示发送的要求写硬盘命令失败. 
	// 于是跳转去处理出现的问题或继续执行下一个硬盘请求, 
	// 否则我们可以向硬盘控制器数据寄存器端口 HD_DATA 写入 1 个扇区的数据.
	if (CURRENT->cmd == WRITE) {
		hd_out(dev, nsect, sec, head, cyl, WIN_WRITE, &write_intr);  // 如果是写操作, 则将中断处理函数设置为 write_intr(). 
		for(i = 0; i < 10000 && !(r = inb_p(HD_STATUS) & DRQ_STAT); i++) {
			/* nothing */ ;
		}
		if (!r) {
			bad_rw_intr();
			goto repeat;							// 该标号在 blk.h 文件最后面.
		}
		port_write(HD_DATA, CURRENT->buffer, 256);
	// 如果当前请求是读硬盘数据, 则向硬盘控制器发送读扇区命令. 若命令无效则停机.
	} else if (CURRENT->cmd == READ) {
		hd_out(dev, nsect, sec, head, cyl, WIN_READ, &read_intr); 	// 如果是读操作, 则将中断处理函数设置为 read_intr().
	} else {
		panic("unknown hd-command");
	}
}

// 硬盘系统初始化.
// 设置硬盘中断描述符, 并允许硬盘控制器发送中断请求信号.
// 该函数设置硬盘设备的请求项处理函数指针为 do_hd_request(), 然后设置硬盘中断门描述符. 
// hd_interrupt(kernel/sys_call.s) 是其中断处理过程地址. 
// 硬盘中断号为 int 0x2E(46), 对应 8259A 芯片的中断请求信号 IRQ13. 
// 接着复位接联的主 8259A int 2 屏蔽位, 允许从片发出中断请求信号. 
// 再复位硬盘的中断请求屏蔽位(在从片上), 允许硬盘控制器发送中断请求信号. 
// 中断描述符表 IDT 内中断门描述符设置宏 set_intr_gate() 在 include/asm/system.h 中实现.
void hd_init(void)
{
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;		// do_hd_request().
	set_intr_gate(0x2E, &hd_interrupt);					// 设置中断门描述符: 对应处理函数指针(kernel/sys_call.s 中)
	outb_p(inb_p(0x21) & 0xfb, 0x21);					// 复位接联的主 8259A int 2 的屏蔽位
	outb(inb_p(0xA1) & 0xbf, 0xA1);						// 复位硬盘中断请求屏蔽位(在从片上).
}
