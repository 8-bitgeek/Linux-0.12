include ../Makefile.header

ifneq ($(V), 1)
	Q=@
endif

LDFLAGS	+= -Ttext 0
CPP_DISPLAY	+= -I../include

## 目标: all
all: bootsect setup head.o

# CPP_DISPLAY = cpp -nostdinc -I../include  (cpp is C Preprocessor)
bootsect.s: bootsect.S ../include/linux/config.h
	$(Q)$(CPP_DISPLAY) -traditional bootsect.S -o bootsect.s
	
# AS86 = as86 -0 -a
# LD86 = ld86 -0
# as86 option: 
# 		-0: start with 16-bit code segment, warn for all instructions > 8086
# 		-a: enable partial compatibility with Minix asld. (兼容 Minix 的 asld)
# ld86 option:
# 		-0: produce header with 16-bit magic
# 		-s: strip symbols
bootsect: bootsect.s
	$(Q)$(AS86) -o bootsect.o bootsect.s
	$(Q)$(LD86) -s -o bootsect bootsect.o
	
setup.s: setup.S ../include/linux/config.h
	$(Q)$(CPP_DISPLAY) -traditional setup.S -o setup.s
	
setup: setup.s
	$(Q)$(AS86) -o setup.o setup.s
	$(Q)$(LD86) -s -o setup setup.o

# AS = as --32
# 		--32: implies Intel i386 architecture.
head.o: head.s
	$(Q)$(AS) -o head.o head.s
	
clean:
	$(Q)rm -f bootsect bootsect.o bootsect.s setup setup.o setup.s head.o