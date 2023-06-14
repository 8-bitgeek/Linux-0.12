/*
 *  linux/lib/dup.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>	                            // Linux 标准头文件. 定义了各种符号常数和类型, 并声明了各种函数.
			                                    // 如定义了 __LIBRARY__, 则还含系统调用号和内嵌汇编 _syscall0() 等.

// 复制文件描述符函数.
// 下面该调用宏函数对应: int dup(int fd). 直接调用了系统中断 int 0x80, 参数是 __NR_dup.
// 其中 fd 是文件描述符.
_syscall1(int, dup, int, fd)
