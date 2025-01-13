
CC = gcc
AR = ar

SRC_DIR := src
SRC_FILES := $(wildcard $(SRC_DIR)/*.c)
OBJ_FILES := $(SRC_FILES:$(SRC_DIR)/%.c=%.o)
ARCH_OBJ = lib/libminicrt.a

ifneq ($(V), 2)
CC_FLAG = -c -m32 -ggdb -O0 -fno-builtin -nostdlib -fno-stack-protector -I include
BUILD_FLAG = -m32 -ggdb -g3 -O0 -fno-omit-frame-pointer -fno-builtin -nostdlib -fno-stack-protector -static -e mini_crt_entry 
else
CC_FLAG = -c -nostdlib
BUILD_FLAG = -nostdlib -e _mini_crt_entry 
endif

all: archive
	rm -rf *.o

compile: $(SRC_FILES)
	$(CC) $(CC_FLAG) $^

archive: compile 
	@mkdir -p lib
	$(AR) -rcs $(ARCH_OBJ) $(OBJ_FILES)

clean: 
	rm -rf *.o lib