SYSTEM := $(shell uname -a | grep " 0.12 " >/dev/null 2>&1 && echo "Linux-0.12" || echo "Modern Linux")
ifeq ($(SYSTEM), Linux-0.12)
V = 2
else
V = 1
endif

CC = gcc
out = test
LIB_DIR = ../minicrt/lib
INC_DIR = ../minicrt/include

ifneq ($(V), 2)
BUILD_FLAG = -m32 -ggdb -g3 -O0 -fno-omit-frame-pointer -fno-builtin -nostdlib -fno-stack-protector -static -e mini_crt_entry -I $(INC_DIR) -L $(LIB_DIR)
else
BUILD_FLAG = -nostdlib -e _mini_crt_entry -I $(INC_DIR) -L $(LIB_DIR)
endif

build: env-info clean test.c elfreader.c
	$(CC) $(BUILD_FLAG) test.c -l minicrt -o $(out)
	$(CC) $(BUILD_FLAG) elfreader.c -l minicrt -o elfreader

debug: clean build
	@if command -v cgdb >/dev/null 2>&1; then \
		echo "cgdb found, using cgdb..."; \
		cgdb ./$(aout); \
	else \
		echo "cgdb not found, falling back to gdb..."; \
		gdb ./$(aout); \
	fi

clean: 
	rm -rf $(out) temp.txt

env-info:
	@echo "OS TYPE: $(SYSTEM)"
