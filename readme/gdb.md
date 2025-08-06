### 打印各个寄存器的信息(qemu gdb only), 可以查看详细信息，比如 DPL

```gdb
-exec monitor info registers

# 输出示例:
ES =  0010        00000000  00ffffff   00c09300    DPL=0    DS   [-WA]
     段选择符       段基地址    段限长     属性字段      DPL          权限标志: (R - 可读, W - 可写, A - 已访问)
```

### 使用给定地址打断点

```gdb
-exec b *0x6997
```

### 打印指定地址的指定

```gdb
-exec b 0x7a28
```

### 指印寄存器指定的地址的命令

```gdb
-exec b $eip
```

### 观察指定内存地址中的数据

```gdb
-exec watch *0x8888
```

### 打印指定内存中的数据

cmd: `x /nfu addr`

n: the repeat count
f: the display format, x - hexadecimal, o - octal, d - signed decimal, u - unsigned decimal, a - address(指针值), c - character, i - instruction, f - float, t - binary, s - string
u: the unit size, b - bytes, h - half word(2bytes), w - words(4bytes), g - Giant words(8bytes)

```gdb
# 以 hex 打印内存 0x8888 开始后的 10 个 words
-exec x /10xw *0x8888
```

### 打印当前 eip 后续的 10 条指令

```gdb
-exec x /10i $eip
```

### 在内核态下观察进程的内存空间中的变量值

原理是: 进程的 nr * 64MB 得到的进程的基地址, 加上变量的偏移地址即是变量的线性地址, 由于使用同一个页目录表, 所以即可从对应的物理中获取变量的值.

```gdb
-exec p task
-exec p current
# 根据地址找到任务对应的任务项号 task_nr
-exec x /10x (task_nr * 64 * 1024 * 1024 + param_ptr)
# 比如 param 内存地址是 0x03, 当前进程在任务列表中的下标是 4, 即任务列表中的第 5 个任务, 则:
-exec x /10x (4 * 64 * 1024 * 1024 + 0x3)
```
