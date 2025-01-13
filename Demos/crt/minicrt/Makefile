objects = malloc.o printf.o stdio.o string.o entry.o
archive_obj = minicrt.a
SRC = $(wildcard *.c)
OBJS = $(SRC:.c=.o)

CC = gcc
AR = ar
ld = LD

ifneq ($(V), 2)
CC_FLAG = -c -m32 -ggdb -O0 -fno-builtin -nostdlib -fno-stack-protector
BUILD_FLAG = -m32 -ggdb -g3 -O0 -fno-omit-frame-pointer -fno-builtin -nostdlib -fno-stack-protector -static -e mini_crt_entry 
else
CC_FLAG = -c -nostdlib
BUILD_FLAG = -nostdlib -e _mini_crt_entry 
endif

all: archive

compile: $(SRC)
	$(CC) $(CC_FLAG) $^

archive: compile 
	$(AR) -rcs minicrt.a $(objects)

clean: 
	rm -rf *.o minicrt.a