# MiniCRT README

# How to compile this project

## 1. Compile by using GNU tools

```shell
cd minicrt
gcc -c -m32 -fno-builtin -nostdlib -fno-stack-protector src/entry.c src/malloc.c src/stdio.c src/string.c src/printf.c
ar -rcs lib/libminicrt.a malloc.o printf.o stdio.o string.o
```
gcc 参数说明: 

- `-fno-builtin`: 关闭 GCC 的内置函数功能, 默认情况下 GCC 会把 strlen, strcmp 等常用函数展开成它内部的实现.
- `-nostdlib`: 不使用任何来自 Glibc, GCC 库文件和启动文件. 包含了 -nostartfiles 这个参数.
- `-fno-stack-protector`: 关闭堆栈保护功能, 新版本的 GCC 会在 vfprintf 这样的变长参数函数中插入堆栈保护代码, 如果不关闭, 在使用 MiniCRT 时会发生 "__stack_chk_fail" 函数未定义的错误.

ar 参数说明: 

- `-r`: replace, 将文件添加到归档中, 如果已存在则替换.
- `-c`: create, 归档文件不存在, 则创建一个新的归档文件.
- `-s`: index, 在归档文件中哉更新符号表索引(symbol index), 符号表帮助链接器快速找到归档中的符号.

## 2. Compile by using GNU Make

```shell
cd minicrt && make archive
```

# How to use MiniCRT

```shell
cd test
gcc -c -m32 -ggdb -g3 -O0 -fno-omit-frame-pointer -fno-builtin -nostdlib -fno-stack-protector -I ../minicrt/include test.c
ld -m elf_i386 -static -e mini_crt_entry -L ../minicrt/lib/ -l minicrt test.o -o test
```

gcc 参数说明: 

- `-ggdb`: 生成 GDB 特定的调试信息, 通常与 `-O0` 和 `-fno-omit-frame-pointer` 一起使用.
- `-g3`: 生成最详细的调试信息, 包含宏定义.
- `-O0`: 关闭优化, 避免编译器对代码优化导致调试信息不准确.
- `-fno-omit-frame-pointer`: 保留帧指针寄存器, 便于调试栈帧.
- `-I`: 指定 .h 头文件查找目录.

ld 参数说明: 

- `-e`: 用于指定入口函数.
- `-m elf_i386`: 生成 32 位的可执行文件.
- `-L`: 指定库文件查找目录.
- `-l minicrt`: 指定依赖的库文件.
