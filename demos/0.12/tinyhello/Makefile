CC = gcc
LD = ld
CFLAGS = -c 

tinyhello: tinyhello.o tinylib.o tinylib.h
	$(LD) -e _nomain -o tinyhello tinyhello.o tinylib.o

.c.o:
	$(CC) $(CFLAGS) -o $*.o $<

run: tinyhello
	./tinyhello

clean:
	rm -f tinyhello *.o
