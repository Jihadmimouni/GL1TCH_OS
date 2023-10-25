cc?=gcc
BUILD_DIR?=build

.PHONY: all build clean always

all: build

build: always
	$(cc) -o $(BUILD_DIR)/hello1.bin hello.c
	$(cc) -o $(BUILD_DIR)/fat.bin fat/fat.c

always:
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)/*