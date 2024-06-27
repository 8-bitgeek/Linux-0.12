CC = gcc
LD = ld
CFLAGS = -c -m32 -fno-builtin 

tinyhello: tinyhello.o tinylib.o tinylib.h
	$(LD) -static -m elf_i386 -e nomain -o tinyhello tinyhello.o tinylib.o

.c.o:
	$(CC) $(CFLAGS) -o $*.o $<

run: tinyhello
	./tinyhello

clean:
	rm -f tinyhello *.o
