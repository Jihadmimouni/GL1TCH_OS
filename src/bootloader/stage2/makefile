BUILD_DIR?=build
ASM?=nasm
CC16?=/usr/bin/watcom/binl64/wcc 
LD16?=/usr/bin/watcom/binl64/wlink

.PHONY: all stage2 clean

all: always stage2

stage2: $(BUILD_DIR)/stage2.bin always

$(BUILD_DIR)/stage2.bin:
		$(ASM) stage2.asm -f bin -o $(BUILD_DIR)/stage2.bin

always:
		mkdir -p $(BUILD_DIR)




clean:
		rm -rf $(BUILD_DIR)/stage2.bin