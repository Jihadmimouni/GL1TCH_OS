cc?=gcc
BUILD_DIR?=build
CC16?=/usr/bin/watcom/binl64/wcc 
LD16?=/usr/bin/watcom/binl64/wlink

.PHONY: all build clean always

all: build

build: always
	$(cc) -o $(BUILD_DIR)/hello1.bin hello.c
	$(cc) -o $(BUILD_DIR)/fat.bin fat/fat.c

always:
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)/*