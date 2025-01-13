objects = malloc.o printf.o stdio.o string.o
archive_obj = minicrt.a
aout = test

CC = gcc
AR = ar
ld = LD

ifneq ($(V), 1)
CC_FLAG = -c -m32 -ggdb -O0 -fno-builtin -nostdlib -fno-stack-protector
BUILD_FLAG = -m32 -ggdb -g3 -O0 -fno-omit-frame-pointer -fno-builtin -nostdlib -fno-stack-protector -static -e mini_crt_entry 
else
CC_FLAG = -c -nostdlib
BUILD_FLAG = -nostdlib -e _mini_crt_entry 
endif


build: archive test.c 
	$(CC) $(BUILD_FLAG) entry.o test.c minicrt.a -o test

compile: entry.c malloc.c stdio.c string.c printf.c
	$(CC) $(CC_FLAG) entry.c malloc.c stdio.c string.c printf.c 

archive: $(objects) compile 
	$(AR) -rcs minicrt.a $(objects)

debug:
	@if command -v cgdb >/dev/null 2>&1; then \
		echo "cgdb found, using cgdb..."; \
		cgdb ./test; \
	else \
		echo "cgdb not found, falling back to gdb..."; \
		gdb ./test; \
	fi

run: build
	./test

clean: 
	rm -rf *.o minicrt.a test temp.txt