# Linux-0.12 kernel source code with Chinese comments

> Linux-0.12 Kernel source code with comments & Compile with GCC 4.8.5, you can run it with QEMU(recommend) or Bochs.

## prerequirement

### Run locally
1. GCC v4.8.5
2. qemu 6.2.0 or higher
3. make
4. bin86 package in Ubuntu

### Run on Docker (Recommend)

1. docker
2. qemu 6.2.0 or higher
3. make
4. gdb (for debug purpose)

## Quick start

```shell
$ make start
```

## How to debug

```shell
$ make debug
# then make your IDE(like vscode) connect it with localhost:1234(use any port you want by update Makefile target)
```
