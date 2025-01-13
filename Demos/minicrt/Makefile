objects = malloc.o printf.o stdio.o string.o
archive_obj = minicrt.a
aout = test

CC = gcc
AR = ar

build: archive test.c 
	$(CC) -m32 -ggdb -g3 -O0 -fno-omit-frame-pointer -fno-builtin -nostdlib -fno-stack-protector -static -e mini_crt_entry entry.o test.c minicrt.a -o test

compile: entry.c malloc.c stdio.c string.c printf.c
	$(CC) -c -m32 -fno-builtin -nostdlib -fno-stack-protector entry.c malloc.c stdio.c string.c printf.c 

archive: $(objects) compile 
	$(AR) -rcs minicrt.a $(objects)

debug: build
	cgdb ./test

run: build
	./test

clean: 
	rm -rf *.o minicrt.a test temp.txt