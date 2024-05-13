CC = gcc
LD = ld

TinyHelloWorld: TinyHelloWorld.c tinylib.c tinylib.h
	$(CC) -c -m32 -fno-builtin -nostdlib -I./ TinyHelloWorld.c
	$(CC) -c -m32 -fno-builtin tinylib.c
	$(LD) -static -m elf_i386 -e nomain -o TinyHelloWorld TinyHelloWorld.o tinylib.o

clean:
	rm -f TinyHelloWorld *.o
