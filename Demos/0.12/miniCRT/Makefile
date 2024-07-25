V = 1
ifneq ($(V), 1)
	Q = @
endif

CC = gcc
LD = ld
DEBUG_TOOL = cgdb

# -fno-builtin 关闭 GCC 的内置函数功能， 默认情况下 GCC 会把 strlen, strcmp 等这些常用函数展开成它的内部实现.
# -nostdlib 表示不使用任务来自 Glibc, gcc 库文件和启动文件, 它包含了 -nostartfiles 这个参数.
# -fno-stack-protector 是关闭堆栈保护功能, 新版本的 GCC 会在 vfprintf 这样的变长参数函数中插入堆栈保护函数， 
#					   如果不关闭会发生 "__stack_chk_fail" 函数未定义的错误.
CFLAGS = -c -g -nostdlib 
LDFLAGS = -e _mini_crt_entry

OBJS = malloc.o printf.o stdio.o string.o entry.o

# $^ 措代所有前置条件
# $@ 措代当前目标
# ar 选项: 
# 	-r: replace existing or insert new file into the archive
# 	-s: act as ranlib
minicrt.a: $(OBJS)
	$(Q)ar -rs $@ $^ 				

# $* 指代所匹配的字符串
# $* 指代第一个前置条件
.c.o: 
	$(Q)$(CC) $(CFLAGS) -o $*.o $<

test: minicrt.a test.o entry.o
	$(Q)$(LD) $(LDFLAGS) entry.o test.o minicrt.a -o test

run: test
	$(Q)./test

debug: test
	$(Q)$(DEBUG_TOOL) test

clean: 
	$(Q)rm -f *.o minicrt.h.gch minicrt.a test
