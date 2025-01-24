SYSTEM := $(shell uname -a | grep " 0.12 " >/dev/null 2>&1 && echo "Linux-0.12" || echo "Modern Linux")
ifeq ($(SYSTEM), Linux-0.12)
V = 2
else
V = 1
endif

CC = gcc
out = test
LIB_DIR = ../../crt/minicrt/lib
INC_DIR = ../../crt/minicrt/include

ifneq ($(V), 2)
BUILD_FLAG = -m32 -ggdb -g3 -O0 -fno-omit-frame-pointer -fno-builtin -nostdlib -fno-stack-protector -static -e mini_crt_entry -I $(INC_DIR) -L $(LIB_DIR)
else
BUILD_FLAG = -nostdlib -e _mini_crt_entry -I $(INC_DIR) -L $(LIB_DIR)
endif

cpmpile: 
	$(CC) $(BUILD_FLAG) -l minicrt minishell.c -o minishell