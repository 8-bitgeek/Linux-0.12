# Linux-0.12 kernel source code with chinese comments

> Linux-0.12 Kernel code with comments & Compile with GCC 4.8.5, can run it with QEMU(recommend) or Bochs.

ref: https://github.com/sky-big/Linux-0.12

## prerequirement

1. GCC v4.8.5
2. qemu 6.2.0 or higher
3. make
4. bin86 package in Ubuntu

## Quick start

```shell
$ make start
```

## How to debug

```shell
$ make debug
# then you can your IDE connect it with localhost:1234(you can use any port you want by modifing Makefile rules)
```
