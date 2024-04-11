CC = gcc

# -fno-builtin 关闭 GCC 的内置函数功能， 默认情况下 GCC 会把 strlen, strcmp 等这些常用函数展开成它的内部实现.
# -nostdlib 表示不使用任务来自 Glibc, gcc 库文件和启动文件, 它包含了 -nostartfiles 这个参数.
# -fno-stack-protector 是关闭堆栈保护功能, 新版本的 GCC 会在 vfprintf 这样的变长参数函数中插入堆栈保护函数， 
#					   如果不关闭会发生 "__stack_chk_fail" 函数未定义的错误.
CC_FLAG = -c -m32 -fno-builtin -nostdlib -fno-stack-protector

# $^ 表示所有前置条件
# $@ 表示当前目标
minicrt.a: malloc.o printf.o stdio.o string.o
	ar -rs $@ $^ 				# -r replace existing or insert new file into the archive
								# -s act as ranlib

all: malloc.c stdio.c # entry.c string.c printf.c
	$(CC) $(CC_FLAG) $^

clean: *.o
	rm -rf *.o
