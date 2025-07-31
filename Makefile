all: 
	$(MAKE) -C src

clean:
	$(MAKE) -C src clean

debug:
	$(MAKE) -C src debug

start:
	$(MAKE) -C src start

bootimage:
	$(MAKE) -C src bootimage
