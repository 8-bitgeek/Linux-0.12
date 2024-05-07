# if you want the ram-disk device, define this to be the size in blocks.
RAMDISK =  #-DRAMDISK=1024

# This is a basic Makefile for setting the general configuration
include Makefile.header

# -Ttext org: 
# 	Locate text section in the output file at the absolute address given by org(0).
# -e entry:
#	Use entry as the explicit symbol for beginning execution of you program, 
# 	rather than the default entry point. If there is no symbol named entry, 
# 	the linker will try to parse entry as a number, and use that as the entry address.
LDFLAGS	+= -Ttext 0 -e startup_32
CFLAGS	+= $(RAMDISK)
CPP	+= -Iinclude

#
# ROOT_DEV specifies the default root-device when making the image.
# This can be either FLOPPY, /dev/xxxx or empty, in which case the
# default of /dev/hd6 is used by 'build'.
#
ROOT_DEV=0301
SWAP_DEV=0304

ARCHIVES=kernel/kernel.o mm/mm.o fs/fs.o
DRIVERS =kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a
MATH	=kernel/math/math.a
LIBS	=lib/lib.a

# -nostdinc: 
#   Do not search the standard system directories for header fles. 
# 	Only the directories you have specifed with ‘-I’ options 
# 	(and the directory of the current file, if appropriate) are searched.
# -I: 
# 	Add the directory dir to the list of directories to be searched for header files during preprocessing.
.c.s:
	$(CC) $(CFLAGS) -nostdinc -Iinclude -S -o $*.s $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -nostdinc -Iinclude -c -o $*.o $<


all: clean Image

# strip: 
# 		strip symbol and sections like debug info from object.
# objcopy 选项解释: 
# 		-O binary: 指定输出目标文件(system.tmp)的格式(bfdname)为 binary.
# 		-R: 去掉源文件中的 .note .comment 区(section)再输出到目标文件.
# sync: synchronize cached writes to persistant storage.
Image: boot/bootsect boot/setup tools/system
	@cp -f tools/system system.tmp
	@strip system.tmp
	@objcopy -O binary -R .note -R .comment system.tmp tools/kernel
# There is no Kernal_Image at begain, we create it by build.sh
	@tools/build.sh boot/bootsect boot/setup tools/kernel Kernel_Image $(ROOT_DEV) $(SWAP_DEV)
	@rm system.tmp
	@rm -f tools/kernel
	@sync

# run `objdump -m i8086 -b binary --start-address=0x20 -D boot/bootsect` to dissamble it.
boot/bootsect: boot/bootsect.S
	@make bootsect -C boot

boot/setup: boot/setup.S
	@make setup -C boot

# objdump -S: 
#	Display source code intermixed with disassembly, if possible.
#
# ARCHIVES	=	kernel/kernel.o mm/mm.o fs/fs.o
# DRIVERS 	=	kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a
# MATH		=	kernel/math/math.a
# LIBS		=	lib/lib.a
tools/system: boot/head.o init/main.o $(ARCHIVES) $(DRIVERS) $(MATH) $(LIBS)
	$(LD) $(LDFLAGS) boot/head.o init/main.o \
		  $(ARCHIVES) $(DRIVERS) $(MATH) $(LIBS) -o tools/system
	@nm tools/system | grep -v '\(compiled\)\|\(\.o$$\)\|\( [aU] \)\|\(\.\.ng$$\)\|\(LASH[RL]DI\)'| sort > System.map
	@objdump -S tools/system > system.S

# make -C {dir}: 
# 	Change to directory {dir} before reading the makefiles or doing anything else.
boot/head.o: boot/head.s
	@make head.o -C boot

# Dependencies:
init/main.o: init/main.c include/unistd.h include/sys/stat.h \
	include/sys/types.h include/sys/time.h include/time.h \
	include/sys/times.h include/sys/utsname.h include/sys/param.h \
	include/sys/resource.h include/utime.h include/linux/tty.h \
	include/termios.h include/linux/sched.h include/linux/head.h \
	include/linux/fs.h include/linux/mm.h include/linux/kernel.h \
	include/signal.h include/asm/system.h include/asm/io.h include/stddef.h \
	include/stdarg.h include/fcntl.h include/string.h

kernel/kernel.o:
	@make -C kernel

mm/mm.o:
	@make -C mm

fs/fs.o:
	@make -C fs

kernel/blk_drv/blk_drv.a:
	@make -C kernel/blk_drv

kernel/chr_drv/chr_drv.a:
	@make -C kernel/chr_drv

kernel/math/math.a:
	@make -C kernel/math

lib/lib.a:
	@make -C lib

tools/build: tools/build.c
	$(CC) $(CFLAGS) -o tools/build tools/build.c

clean:
	@rm -f Kernel_Image System.map System_s.map system.S tmp_make core boot/bootsect boot/setup
	@rm -f init/*.o tools/system boot/*.o typescript* info bochsout.txt
	@for i in mm fs kernel lib boot miniCRT; do make clean -C $$i; done

debug:
	@dd if=Kernel_Image of=images/boota.img bs=512 conv=notrunc,sync
	@qemu-system-i386 -m 32M -boot a -fda images/boota.img -fdb images/rootimage-0.12-fd -hda images/rootimage-0.12-hd \
	-serial pty -S -gdb tcp::1234

start:
	@dd if=Kernel_Image of=images/boota.img bs=512 conv=notrunc,sync
	@qemu-system-i386 -m 32M -boot a -fda images/boota.img -fdb images/rootimage-0.12-fd -hda images/rootimage-0.12-hd

dep:
	@sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	@(for i in init/*.c; do echo -n "init/"; $(CPP) -M $$i; done) >> tmp_make
	@cp tmp_make Makefile
	@for i in fs kernel mm lib; do make dep -C $$i; done