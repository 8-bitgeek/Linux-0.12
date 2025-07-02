MAKE = make
SUBDIRS := minicrt test

.PHONY: all clean minicrt test

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
	rm -f temp.txt


