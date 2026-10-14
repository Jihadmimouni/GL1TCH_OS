cc?=gcc
ASM?=nasm
BUILD_DIR?=build
CC16?=/usr/bin/watcom/binl64/wcc
LD16?=/usr/bin/watcom/binl64/wlink

.PHONY: all build clean always

all: build

build: always
	$(ASM) -f bin -o $(BUILD_DIR)/hello_rm.bin hello_rm.asm
	$(cc) -o $(BUILD_DIR)/fat.bin fat/fat.c

always:
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)/*