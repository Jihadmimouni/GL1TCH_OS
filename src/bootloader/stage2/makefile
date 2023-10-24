BUILD_DIR?=/build
ASM?=nasm

.PHONY: all stage2 clean

all: stage2

stage2: $(BUILD_DIR)/stage2.bin

$(BUILD_DIR)/stage2.bin:
		$(ASM) stage2.asm -f bin -o $(BUILD_DIR)/stage2.bin


clean:
		rm -rf $(BUILD_DIR)/stage2.bin