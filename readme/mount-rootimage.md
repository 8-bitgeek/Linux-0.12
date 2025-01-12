# How to mount rootimage-0.12-hd

## 1. Get rootimage-0.12-hd's partition info
```shell
# losetup -f --show rootimage-0.12-hd   自动查找一个空闲的环回设备并将文件与之关联, 并返回设备名
losetup /dev/loop1 rootimage-0.12-hd
fdisk /dev/loop1
# 以下命令在 fdisk 控制台执行
x                               # enable expert
p                               # print the partition table
```

以下是 fdisk 中执行 `p` 命令的输出: 
Disk /dev/loop1: 239.7 MiB, 251338752 bytes, 490896 sectors
Units: sectors of 1 * 512 = 512 bytes
Sector size (logical/physical): 512 bytes / 512 bytes
I/O size (minimum/optimal): 512 bytes / 512 bytes
Disklabel type: dos
Disk identifier: 0x00000000

Device       Boot  Start    End Sectors Id Type                 Start-C/H/S End-C/H/S Attrs
/dev/loop1p1 *         1 132047  132047 81 Minix / old Linux          0/1/1 130/15/63    80
/dev/loop1p2      132048 264095  132048 81 Minix / old Linux        131/0/1 261/15/63 
/dev/loop1p3      264096 396143  132048 81 Minix / old Linux        262/0/1 392/15/63 
/dev/loop1p4      396144 478799   82656 82 Linux swap / Solaris     393/0/1 474/15/63 

从上面的 fdisk 输出的分区信息可以看出, 该 Images 文件中含有 3 个 Minix 分区(ID = 81)和 1 个交换分区(ID = 82), 如果要访问第一个分区, 则记录起始扇区号(Start-C/H/S) S = 1, 

## 2. Mount rootimage-0.12-hd to ./mnt

```shell
losetup -d /dev/loop1
losetup -o 512 /dev/loop1 rootimage-0.12-hd             # 1 sector = 512B, so offset is 512
mount -t minix /dev/loop1 ./mnt
cd ./mnt && ls
```

## 3. Umount rootimage-0.12-hd

```shell
umount /dev/loop1
losetup -d /dev/loop1
```

mount 参数说明: 

- `-t minix`: 指明文件系统类型是 MINIX
- `-o loop`: 通过 loop 设备来加载文件系统