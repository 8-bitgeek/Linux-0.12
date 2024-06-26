#
# Makefile for the FREAX-kernel block device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

include ../../Makefile.header

ifneq ($(V), 1)
	Q=@
endif

CFLAGS	+= -finline-functions -nostdinc -I../../include
CPP	+= -I../../include

.c.s:
	$(Q)$(CC) $(CFLAGS) -S -o $*.s $<
.s.o:
	$(Q)$(AS) -o $*.o $<
.c.o:
	$(Q)$(CC) $(CFLAGS) -c -o $*.o $<

OBJS  = ll_rw_blk.o floppy.o hd.o ramdisk.o
	# ll_rw_blk.o floppy.o hd.o ramdisk.o
blk_drv.a: $(OBJS)
	$(Q)$(AR) rcs blk_drv.a $(OBJS)
	$(Q)sync

clean:
	$(Q)rm -f core *.o *.a tmp_make
	$(Q)for i in *.c; do rm -f `basename $$i .c`.s; done

dep:
	$(Q)sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	$(Q)(for i in *.c;do echo -n `echo $$i | sed 's,\.c,\.s,'`" "; \
		$(CPP) -M $$i;done) >> tmp_make
	$(Q)cp tmp_make Makefile

### Dependencies:
floppy.s floppy.o: floppy.c ../../include/linux/sched.h ../../include/linux/head.h \
 ../../include/linux/fs.h ../../include/sys/types.h \
 ../../include/linux/mm.h ../../include/linux/kernel.h \
 ../../include/signal.h ../../include/sys/param.h \
 ../../include/sys/time.h ../../include/time.h \
 ../../include/sys/resource.h ../../include/linux/fdreg.h \
 ../../include/asm/system.h ../../include/asm/io.h \
 ../../include/asm/segment.h blk.h
hd.s hd.o: hd.c ../../include/linux/config.h ../../include/linux/sched.h \
 ../../include/linux/head.h ../../include/linux/fs.h \
 ../../include/sys/types.h ../../include/linux/mm.h \
 ../../include/linux/kernel.h ../../include/signal.h \
 ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h \
 ../../include/sys/resource.h ../../include/linux/hdreg.h \
 ../../include/asm/system.h ../../include/asm/io.h blk.h
ll_rw_blk.s ll_rw_blk.o: ll_rw_blk.c ../../include/errno.h \
 ../../include/linux/sched.h ../../include/linux/head.h \
 ../../include/linux/fs.h ../../include/sys/types.h \
 ../../include/linux/mm.h ../../include/linux/kernel.h \
 ../../include/signal.h ../../include/sys/param.h \
 ../../include/sys/time.h ../../include/time.h \
 ../../include/sys/resource.h ../../include/asm/system.h blk.h
ramdisk.s ramdisk.o: ramdisk.c ../../include/string.h ../../include/linux/fs.h \
 ../../include/sys/types.h blk.h ../../include/linux/kernel.h \
 ../../include/linux/sched.h ../../include/linux/head.h \
 ../../include/linux/mm.h ../../include/signal.h \
 ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h \
 ../../include/sys/resource.h
