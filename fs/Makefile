include ../Makefile.header

ifneq ($(V), 1)
	Q=@
endif

LDFLAGS += -r
CFLAGS	+= -fno-defer-pop -nostdinc -I../include
CPP	+= -I../include

.c.s:
	$(Q)$(CC) $(CFLAGS) -S -o $*.s $<
.c.o:
	$(Q)$(CC) $(CFLAGS) -c -o $*.o $<
.s.o:
	$(AS) -o $*.o $<

OBJS = open.o read_write.o inode.o file_table.o buffer.o super.o \
	   block_dev.o char_dev.o file_dev.o stat.o exec.o pipe.o namei.o \
	   bitmap.o fcntl.o ioctl.o truncate.o select.o

fs.o: $(OBJS)
	$(Q)$(LD) $(LDFLAGS) -o fs.o $(OBJS)

clean:
	$(Q)rm -f core *.o *.a tmp_make
	$(Q)for i in *.c; do rm -f `basename $$i .c`.s; done

dep:
	$(Q)sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	$(Q)(for i in *.c; do $(CPP) -M $$i; done) >> tmp_make
	$(Q)cp tmp_make Makefile

### Dependencies:
bitmap.o: bitmap.c ../include/string.h ../include/linux/sched.h \
 ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
 ../include/linux/mm.h ../include/linux/kernel.h ../include/signal.h \
 ../include/sys/param.h ../include/sys/time.h ../include/time.h \
 ../include/sys/resource.h
block_dev.o: block_dev.c ../include/errno.h ../include/linux/sched.h \
 ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
 ../include/linux/mm.h ../include/linux/kernel.h ../include/signal.h \
 ../include/sys/param.h ../include/sys/time.h ../include/time.h \
 ../include/sys/resource.h ../include/asm/segment.h
buffer.o: buffer.c ../include/stdarg.h ../include/linux/sched.h \
 ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
 ../include/linux/mm.h ../include/linux/kernel.h ../include/signal.h \
 ../include/sys/param.h ../include/sys/time.h ../include/time.h \
 ../include/sys/resource.h ../include/asm/system.h
char_dev.o: char_dev.c ../include/errno.h ../include/sys/types.h \
 ../include/linux/sched.h ../include/linux/head.h ../include/linux/fs.h \
 ../include/linux/mm.h ../include/linux/kernel.h ../include/signal.h \
 ../include/sys/param.h ../include/sys/time.h ../include/time.h \
 ../include/sys/resource.h ../include/asm/segment.h ../include/asm/io.h
exec.o: exec.c ../include/errno.h ../include/string.h \
 ../include/sys/stat.h ../include/sys/types.h ../include/a.out.h \
 ../include/linux/fs.h ../include/linux/sched.h ../include/linux/head.h \
 ../include/linux/mm.h ../include/linux/kernel.h ../include/signal.h \
 ../include/sys/param.h ../include/sys/time.h ../include/time.h \
 ../include/sys/resource.h ../include/asm/segment.h
fcntl.o: fcntl.c ../include/errno.h ../include/linux/sched.h \
 ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
 ../include/linux/mm.h ../include/linux/kernel.h ../include/signal.h \
 ../include/sys/param.h ../include/sys/time.h ../include/time.h \
 ../include/sys/resource.h ../include/fcntl.h
file_dev.o: file_dev.c ../include/errno.h ../include/fcntl.h \
 ../include/sys/types.h ../include/linux/sched.h ../include/linux/head.h \
 ../include/linux/fs.h ../include/linux/mm.h ../include/linux/kernel.h \
 ../include/signal.h ../include/sys/param.h ../include/sys/time.h \
 ../include/time.h ../include/sys/resource.h ../include/asm/segment.h
file_table.o: file_table.c ../include/linux/fs.h ../include/sys/types.h
inode.o: inode.c ../include/string.h ../include/sys/stat.h \
 ../include/sys/types.h ../include/linux/sched.h ../include/linux/head.h \
 ../include/linux/fs.h ../include/linux/mm.h ../include/linux/kernel.h \
 ../include/signal.h ../include/sys/param.h ../include/sys/time.h \
 ../include/time.h ../include/sys/resource.h ../include/asm/system.h
ioctl.o: ioctl.c ../include/errno.h ../include/sys/stat.h \
 ../include/sys/types.h ../include/linux/sched.h ../include/linux/head.h \
 ../include/linux/fs.h ../include/linux/mm.h ../include/linux/kernel.h \
 ../include/signal.h ../include/sys/param.h ../include/sys/time.h \
 ../include/time.h ../include/sys/resource.h
namei.o: namei.c ../include/linux/sched.h ../include/linux/head.h \
 ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
 ../include/linux/kernel.h ../include/signal.h ../include/sys/param.h \
 ../include/sys/time.h ../include/time.h ../include/sys/resource.h \
 ../include/asm/segment.h ../include/string.h ../include/fcntl.h \
 ../include/errno.h ../include/const.h ../include/sys/stat.h
open.o: open.c ../include/errno.h ../include/fcntl.h \
 ../include/sys/types.h ../include/utime.h ../include/sys/stat.h \
 ../include/linux/sched.h ../include/linux/head.h ../include/linux/fs.h \
 ../include/linux/mm.h ../include/linux/kernel.h ../include/signal.h \
 ../include/sys/param.h ../include/sys/time.h ../include/time.h \
 ../include/sys/resource.h ../include/linux/tty.h ../include/termios.h \
 ../include/asm/segment.h
pipe.o: pipe.c ../include/errno.h ../include/termios.h \
 ../include/sys/types.h ../include/linux/sched.h ../include/linux/head.h \
 ../include/linux/fs.h ../include/linux/mm.h ../include/linux/kernel.h \
 ../include/signal.h ../include/sys/param.h ../include/sys/time.h \
 ../include/time.h ../include/sys/resource.h ../include/asm/segment.h
read_write.o: read_write.c ../include/sys/stat.h ../include/sys/types.h \
 ../include/errno.h ../include/linux/kernel.h ../include/linux/sched.h \
 ../include/linux/head.h ../include/linux/fs.h ../include/linux/mm.h \
 ../include/signal.h ../include/sys/param.h ../include/sys/time.h \
 ../include/time.h ../include/sys/resource.h ../include/asm/segment.h
select.o: select.c ../include/linux/kernel.h ../include/linux/tty.h \
 ../include/termios.h ../include/sys/types.h ../include/linux/sched.h \
 ../include/linux/head.h ../include/linux/fs.h ../include/linux/mm.h \
 ../include/signal.h ../include/sys/param.h ../include/sys/time.h \
 ../include/time.h ../include/sys/resource.h ../include/asm/segment.h \
 ../include/asm/system.h ../include/sys/stat.h ../include/errno.h
stat.o: stat.c ../include/errno.h ../include/sys/stat.h \
 ../include/sys/types.h ../include/linux/fs.h ../include/linux/sched.h \
 ../include/linux/head.h ../include/linux/mm.h ../include/linux/kernel.h \
 ../include/signal.h ../include/sys/param.h ../include/sys/time.h \
 ../include/time.h ../include/sys/resource.h ../include/asm/segment.h
super.o: super.c ../include/linux/sched.h ../include/linux/head.h \
 ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
 ../include/linux/kernel.h ../include/signal.h ../include/sys/param.h \
 ../include/sys/time.h ../include/time.h ../include/sys/resource.h \
 ../include/asm/system.h ../include/errno.h ../include/sys/stat.h
truncate.o: truncate.c ../include/linux/sched.h ../include/linux/head.h \
 ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
 ../include/linux/kernel.h ../include/signal.h ../include/sys/param.h \
 ../include/sys/time.h ../include/time.h ../include/sys/resource.h \
 ../include/sys/stat.h
