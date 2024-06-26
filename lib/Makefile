#
# Makefile for some libs needed in the kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

include ../Makefile.header

ifneq ($(V), 1)
	Q=@
endif

CFLAGS	+= -finline-functions -nostdinc -I../include
CPP	+= -I../include

.c.s:
	$(Q)$(CC) $(CFLAGS) -S -o $*.s $<
.s.o:
	$(Q)$(AS) -o $*.o $<
.c.o:
	$(Q)$(CC) $(CFLAGS) -c -o $*.o $<

OBJS   = ctype.o _exit.o open.o close.o errno.o write.o dup.o setsid.o \
		 execve.o wait.o string.o malloc.o debug.o
lib.a: $(OBJS)
	$(Q)$(AR) rcs lib.a $(OBJS)
	$(Q)sync

clean:
	$(Q)rm -f core *.o *.a tmp_make
	$(Q)for i in *.c; do rm -f `basename $$i .c`.s; done

dep:
	$(Q)sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	$(Q)(for i in *.c; do echo -n `echo $$i | sed 's,\.c,\.s,'`" "; \
		$(CPP) -M $$i; done) >> tmp_make
	$(Q)cp tmp_make Makefile

### Dependencies:
_exit.s _exit.o: _exit.c ../include/unistd.h ../include/sys/stat.h \
 ../include/sys/types.h ../include/sys/time.h ../include/time.h \
 ../include/sys/times.h ../include/sys/utsname.h ../include/sys/param.h \
 ../include/sys/resource.h ../include/utime.h
close.s close.o: close.c ../include/unistd.h ../include/sys/stat.h \
 ../include/sys/types.h ../include/sys/time.h ../include/time.h \
 ../include/sys/times.h ../include/sys/utsname.h ../include/sys/param.h \
 ../include/sys/resource.h ../include/utime.h
ctype.s ctype.o: ctype.c ../include/ctype.h
debug.s debug.o: debug.c ../include/stdarg.h ../include/linux/kernel.h
dup.s dup.o: dup.c ../include/unistd.h ../include/sys/stat.h \
 ../include/sys/types.h ../include/sys/time.h ../include/time.h \
 ../include/sys/times.h ../include/sys/utsname.h ../include/sys/param.h \
 ../include/sys/resource.h ../include/utime.h
errno.s errno.o: errno.c
execve.s execve.o: execve.c ../include/unistd.h ../include/sys/stat.h \
 ../include/sys/types.h ../include/sys/time.h ../include/time.h \
 ../include/sys/times.h ../include/sys/utsname.h ../include/sys/param.h \
 ../include/sys/resource.h ../include/utime.h
malloc.s malloc.o: malloc.c ../include/linux/kernel.h ../include/linux/mm.h \
 ../include/signal.h ../include/sys/types.h ../include/asm/system.h
open.s open.o: open.c ../include/unistd.h ../include/sys/stat.h \
 ../include/sys/types.h ../include/sys/time.h ../include/time.h \
 ../include/sys/times.h ../include/sys/utsname.h ../include/sys/param.h \
 ../include/sys/resource.h ../include/utime.h ../include/stdarg.h
setsid.s setsid.o: setsid.c ../include/unistd.h ../include/sys/stat.h \
 ../include/sys/types.h ../include/sys/time.h ../include/time.h \
 ../include/sys/times.h ../include/sys/utsname.h ../include/sys/param.h \
 ../include/sys/resource.h ../include/utime.h
string.s string.o: string.c ../include/string.h
wait.s wait.o: wait.c ../include/unistd.h ../include/sys/stat.h \
 ../include/sys/types.h ../include/sys/time.h ../include/time.h \
 ../include/sys/times.h ../include/sys/utsname.h ../include/sys/param.h \
 ../include/sys/resource.h ../include/utime.h ../include/sys/wait.h
write.s write.o: write.c ../include/unistd.h ../include/sys/stat.h \
 ../include/sys/types.h ../include/sys/time.h ../include/time.h \
 ../include/sys/times.h ../include/sys/utsname.h ../include/sys/param.h \
 ../include/sys/resource.h ../include/utime.h
