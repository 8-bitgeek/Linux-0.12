#
# Makefile for the FREAX-kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

include ../Makefile.header

ifneq ($(V), 1)
	Q=@
endif

# -r: Generate relocatable output, generate an output file that can in turn 
# 	  serve as input to ld. this is often called partical linking.
# LDFLAGS = -m elf_i386 -r 
LDFLAGS	+= -r

# NOTE!: do not try to add -On to CFLAGS with gcc4.x, which will optimize the
# memcpy function a lot to let the kernel not work, for fixing this problem,
# please refer to glibc and rewrite the memcpy in include/string.h, or just not
# use any -On options with gcc 4.x when compiling it. in fact, we only can not
# use -On with gcc4.x to compile fork.c, but after that you need to isolate the
# compling procedure of it, it's not good too. for playing with linux-0.11, we
# just have fun, have no much concerning about the performance.

CFLAGS	+= -finline-functions -nostdinc -I../include	
CPP	+= -I../include

# .c 编译成 .s 文件的规则
.c.s:
	$(Q)$(CC) $(CFLAGS) -S -o $*.s $<
# .s 编译成 .o 文件的规则
.s.o:
	$(Q)$(AS) -o $*.o $<
# .c 编译成 .o 文件的规则
.c.o:
	$(Q)$(CC) $(CFLAGS) -c -o $*.o $<

OBJS  = sched.o sys_call.o traps.o asm.o fork.o panic.o printk.o \
		vsprintf.o sys.o exit.o signal.o mktime.o

# LDFLAGS = -m elf_i386 -r 
kernel.o: $(OBJS)
	$(Q)$(LD) $(LDFLAGS) -o kernel.o $(OBJS)
	$(Q)sync

clean:
	$(Q)rm -f core *.o *.a tmp_make keyboard.s
	$(Q)for i in *.c; do rm -f `basename $$i .c`.s; done
	$(Q)for i in chr_drv blk_drv math; do make clean -C $$i; done

dep:
	$(Q)sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	$(Q)(for i in *.c; do echo -n `echo $$i | sed 's,\.c,\.s,'`" "; \
		$(CPP) -M $$i;done) >> tmp_make
	$(Q)cp tmp_make Makefile
	$(Q)for i in chr_drv blk_drv math; do make dep -C $$i; done

### Dependencies:
exit.s exit.o: exit.c ../include/errno.h ../include/signal.h \
 ../include/sys/types.h ../include/sys/wait.h ../include/linux/sched.h \
 ../include/linux/head.h ../include/linux/fs.h ../include/linux/mm.h \
 ../include/linux/kernel.h ../include/sys/param.h ../include/sys/time.h \
 ../include/time.h ../include/sys/resource.h ../include/linux/tty.h \
 ../include/termios.h ../include/asm/segment.h
fork.s fork.o: fork.c ../include/errno.h ../include/linux/sched.h \
 ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
 ../include/linux/mm.h ../include/linux/kernel.h ../include/signal.h \
 ../include/sys/param.h ../include/sys/time.h ../include/time.h \
 ../include/sys/resource.h ../include/asm/system.h
mktime.s mktime.o: mktime.c ../include/time.h
panic.s panic.o: panic.c ../include/linux/kernel.h ../include/linux/sched.h \
 ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
 ../include/linux/mm.h ../include/signal.h ../include/sys/param.h \
 ../include/sys/time.h ../include/time.h ../include/sys/resource.h
printk.s printk.o: printk.c ../include/stdarg.h ../include/stddef.h \
 ../include/linux/kernel.h
sched.s sched.o: sched.c ../include/linux/sched.h ../include/linux/head.h \
 ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
 ../include/linux/kernel.h ../include/signal.h ../include/sys/param.h \
 ../include/sys/time.h ../include/time.h ../include/sys/resource.h \
 ../include/linux/sys.h ../include/linux/fdreg.h ../include/asm/system.h \
 ../include/asm/io.h
signal.s signal.o: signal.c ../include/linux/sched.h ../include/linux/head.h \
 ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
 ../include/linux/kernel.h ../include/signal.h ../include/sys/param.h \
 ../include/sys/time.h ../include/time.h ../include/sys/resource.h \
 ../include/asm/segment.h ../include/errno.h
sys.s sys.o: sys.c ../include/errno.h ../include/linux/sched.h \
 ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
 ../include/linux/mm.h ../include/linux/kernel.h ../include/signal.h \
 ../include/sys/param.h ../include/sys/time.h ../include/time.h \
 ../include/sys/resource.h ../include/linux/config.h \
 ../include/asm/segment.h ../include/sys/times.h ../include/sys/utsname.h \
 ../include/string.h
traps.s traps.o: traps.c ../include/linux/head.h ../include/linux/sched.h \
 ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
 ../include/linux/kernel.h ../include/signal.h ../include/sys/param.h \
 ../include/sys/time.h ../include/time.h ../include/sys/resource.h \
 ../include/asm/system.h ../include/asm/segment.h ../include/asm/io.h
vsprintf.s vsprintf.o: vsprintf.c ../include/stdarg.h ../include/string.h
