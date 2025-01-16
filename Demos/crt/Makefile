MAKE = make
SUBDIRS := minicrt test

all: 
	$(MAKE) -C $@

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done


