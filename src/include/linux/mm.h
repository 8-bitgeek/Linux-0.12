// mm.h 是内存管理头文件. 其中主要定义了内存页面的大小和几个页面释放函数原型.
#ifndef _MM_H
#define _MM_H

#define PAGE_SIZE 4096		                    // 定义 1 页内存页面字节数. 注意高速缓冲块长度是 1024 字节.

#include <linux/kernel.h>
#include <signal.h>

extern int SWAP_DEV;		                   // 内存页面交换设备号. 定义在 mm/swap.c 文件中.

// 从交换设备读入和写出被交换内存页面. ll_rw_page() 定义在 blk_drv/ll_rw_block.c 文件中.
// 参数 nr 是主内存区中页面号; buffer 是读/写缓冲区.
#define read_swap_page(nr, buffer)   ll_rw_page(READ, SWAP_DEV, (nr), (buffer));
#define write_swap_page(nr, buffer)  ll_rw_page(WRITE, SWAP_DEV, (nr), (buffer));

extern unsigned long get_free_page(void);                                           // 在主内存区中取空闲物理页面. 如果已经没有可有内存了, 则返回 0
extern unsigned long put_dirty_page(unsigned long page, unsigned long address);      // 把一内容已修改过的物理内存页面映射到线性地址空间处. 与 put_page() 几乎完全一样。
extern void free_page(unsigned long addr);                                          // 释放物理地址 addr 开始的 1 页面内存。
extern void init_swapping(void);                                                    // 内存交换初始化
void swap_free(int page_nr);                                                        // 释放编号 page_nr 的 1 页面交换页面
void swap_in(unsigned long * table_ptr);                                            // 把页表项是 table_ptr 的一页物理内存换出到交换空间

static inline void oom(void)
{
	printk("Out of memory!!!\n\r");
    // do_exit() 应该使用退出代码, 这里用了信息值 SIGSEGV(11). 相同值的出错码含义是 "资源暂不可用", 正好同义.
	do_exit(SIGSEGV);
}

// 刷新页变换高速缓冲区宏函数.
// 为了提高地址转换的效率, CPU 将最近使用的页表数据存放在芯片中高速缓冲中. 
// 在修改过页表信息之后, 就需要刷新该缓冲区. 
// 这里使用重新加载页目录基地址寄存器 cr3 的方法来进行刷新. 下面 eax = 0 是页目录表的基址(0x0).
#define invalidate() \
__asm__("movl %%eax, %%cr3" : : "a" (0))

/* these are not to be changed without changing head.s etc */
/* 下面定义若需要改动, 则需要与 head.s 等文件的相关信息一起改变. */
// Linux0.12 内核默认支持的最大内存容量是 16MB, 可以修改这些定义以适合更多的内存.
#define LOW_MEM 0x100000			             // 机器物理内存低端(1MB)
extern unsigned long HIGH_MEMORY;		         // 存放实际物理内存最高端地址.
#define PAGING_MEMORY (15 * 1024 * 1024)         // 分页内存 15MB. 主内存区最多 15MB.
#define PAGING_PAGES (PAGING_MEMORY >> 12)	     // 分页后的物理内存页面数(3840).
#define MAP_NR(addr) (((addr) - LOW_MEM) >> 12)	 // 指定内存地址映射为页面号. 2 ^ 12 = 4KB
#define USED 100				                 // 页面被占用标志.

// 内存映射字节图(1 字节代表 1 页内存). 每个页面对应的字节用于标志页面当前被引用(占用)次数. 
// 它最大可以映射 15MB 的内存空间. 在初始化函数 mem_init() 中, 对于不能用作主内存区页面的位置均都参选被设置成 USED(100).
extern unsigned char mem_map [ PAGING_PAGES ];

// 下面定义的符号常量对应页目录表项和页表(二级页表)项中的一些标志位.
#define PAGE_DIRTY	         0x40	            // 位 6 置位, 页面脏(已修改)
#define PAGE_ACCESSED	     0x20	            // 位 5 置位, 页面被访问过.
#define PAGE_USER	         0x04	            // 位 2 置位, 页面属于: 1 - 用户; 0 - 超级用户.
#define PAGE_RW		         0x02	            // 位 1 置位, 读写权: 1 - 写; 0 - 读.
#define PAGE_PRESENT	     0x01	            // 位 0 置位, 页面存在: 1 - 存在; 0 - 不存在.

#endif
