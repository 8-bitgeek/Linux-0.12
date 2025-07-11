#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h>

#define _LOW(v)		((v) & 0377)                    // 取低字节(8 进制表示). 
#define _HIGH(v)	(((v) >> 8) & 0377)             // 取高字节. 

// 以下常数符号是函数 waitpid(pit_t pid, long *stat_addr, int options) 中 options 使用的选项. 
#define WNOHANG		1       // 如果没有处于僵死或停止状态的子进程时不要挂起, 立刻返回. 
#define WUNTRACED	2       // 需要报告已停止执行的子进程状态. 

// 以下宏定义用于判断 waitpid() 函数返回的状态字. 
#define WIFEXITED(s)	(!((s) & 0xFF))             // 如果子进程正常退出, 则为真. 
#define WIFSTOPPED(s)	(((s) & 0xFF) == 0x7F)      // 如果子进程正处于停止状态, 则为真. 
#define WEXITSTATUS(s)	(((s) >> 8) & 0xFF)         // 返回退出状态. 
#define WTERMSIG(s)	    ((s) & 0x7F)                // 返回导致进程终止的信号值(信号量). 
#define WCOREDUMP(s)	((s) & 0x80)                // 判断进程是否执行了内在映像转储(dumpcore). 
#define WSTOPSIG(s)	    (((s) >> 8) & 0xFF)         // 返回导致进程停止的信号值. 
#define WIFSIGNALED(s)	(((unsigned int)(s) - 1 & 0xFFFF) < 0xFF) // 如果由于未捕捉信号而导致子进程退出则为真. 

// wait() 和 waitpid() 函数允许进程获取与其子进程之一的状态信息. 各种选项允许获取已经终止或停止的子进程状态信息. 
// 如果存在两个或两个以上子进程的状态信息, 则报告的顺序是不指定的. wait() 将挂起当前进程, 直到其子进程之一退出(终止), 
// 或者收到要求终止该进程的信号, 或者是需要调用一个信号句柄(信号处理程序). waitpid()挂起当前进程, 
// 直到 pid 指定的子进程退出(终止)或者收到要求终止该进程的信号, 或者是需要调用一个信号句柄(信号处理程序). 
// 如果 pid = -1, options = 0, 则 waitpid() 的作用与 wait() 函数一样. 
// 否则其行为将随 pid 和 options 参数的不同而不同. (参见kernel/exit.c)
// 参数 pid 是进程号; *stat_loc 是保存状态信息位置的指针; options 是等待选项. 
pid_t wait(int *stat_loc);
pid_t waitpid(pid_t pid, int *stat_loc, int options);

#endif
