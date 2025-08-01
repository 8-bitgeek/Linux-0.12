all: 
	$(MAKE) -C src bootimage

clean:
	$(MAKE) -C src clean

debug:
	$(MAKE) -C src debug

start:
	$(MAKE) -C src start
