CC = gcc
LD = ld

run: elf2aout test
	./elf2aout

elf2aout: elf2aout.c a.out.h
	gcc -std=c99 -o elf2aout elf2aout.c

clean:
	rm -rf elf2aout
