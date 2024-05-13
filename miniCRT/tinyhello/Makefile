CC = gcc
LD = ld
CFLAGS = -c -m32 -fno-builtin 

TinyHelloWorld: TinyHelloWorld.o tinylib.o tinylib.h
	$(LD) -static -m elf_i386 -e nomain -o TinyHelloWorld TinyHelloWorld.o tinylib.o

.c.o:
	$(CC) $(CFLAGS) -o $*.o $<

run: TinyHelloWorld
	./TinyHelloWorld

clean:
	rm -f TinyHelloWorld *.o
