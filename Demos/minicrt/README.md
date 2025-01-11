# MiniCRT README

# How to compile this project

```shell
gcc -c -fno-builtin -nostdlib -fno-stack-protector entry.c malloc.c stdio.c string.c printf.c
ar -rs minicrt.a malloc.o printf.o stdio.o string.o
```
- `-fno-builtin`: 关闭 GCC 的内置函数功能, 默认情况下 GCC 会把 strlen, strcmp 等常用函数展开成它内部的实现.
- `-nostdlib`: 不使用任何来自 Glibc, GCC 库文件和启动文件. 包含了 -nostartfiles 这个参数.
- `-fno-stack-protector`: 关闭堆栈保护功能, 新版本的 GCC 会在 vfprintf 这样的变长参数函数中插入堆栈保护代码, 如果不关闭, 在使用 MiniCRT 时会发生 "__stack_chk_fail" 函数未定义的错误.

# How to use MiniCRT

```shell
gcc -c -ggdb -fno-builtin -nostdlib -fno-stack-protector test.c
ld -static -e mini_crt_entry entry.o test.o minicrt.a -o test
```
- `-e`: 用于指定入口函数.