CC = gcc
DEBUG_TOOL = cgdb

# -fno-builtin 关闭 GCC 的内置函数功能， 默认情况下 GCC 会把 strlen, strcmp 等这些常用函数展开成它的内部实现.
# -nostdlib 表示不使用任务来自 Glibc, gcc 库文件和启动文件, 它包含了 -nostartfiles 这个参数.
# -fno-stack-protector 是关闭堆栈保护功能, 新版本的 GCC 会在 vfprintf 这样的变长参数函数中插入堆栈保护函数， 
#					   如果不关闭会发生 "__stack_chk_fail" 函数未定义的错误.
ifeq ($(V), 64)
	CC_FLAG = -c -g -fno-builtin -nostdlib -fno-stack-protector -Wall 
	CC_TEST_FLAG = -c -g -fno-builtin -nostdlib -fno-stack-protector 
	LD_TEST_FLAG = -static -e mini_crt_entry 
else 
	CC_FLAG = -c -g -m32 -fno-builtin -nostdlib -fno-stack-protector 
	CC_TEST_FLAG = -m32 -c -g -fno-builtin -nostdlib -fno-stack-protector 
	LD_TEST_FLAG = -m elf_i386 -static -e mini_crt_entry 
endif

objects = malloc.o printf.o stdio.o string.o entry.o

# $^ 表示所有前置条件
# $@ 表示当前目标
minicrt.a: $(objects)
	ar -rs $@ $^ 				
# -r replace existing or insert new file into the archive
# -s act as ranlib

$(objects): malloc.c stdio.c string.c printf.c entry.c minicrt.h
	$(CC) $(CC_FLAG) $^

clean: 
	rm -rf *.o minicrt.h.gch minicrt.a 

test: minicrt.a test.o entry.o
	gcc $(CC_TEST_FLAG) test.c
	ld $(LD_TEST_FLAG) entry.o test.o minicrt.a -o test 

debug: test
	DEBUG_TOOL test
