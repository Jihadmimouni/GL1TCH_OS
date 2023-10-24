BUILD_DIR?=build
ASM?=nasm

.PHONY: always all stage1 clean 

all: always stage1

stage1: $(BUILD_DIR)/stage1.bin always

$(BUILD_DIR)/stage1.bin:
		$(ASM) stage1.asm -f bin -o $(BUILD_DIR)/stage1.bin

always:
		mkdir -p $(BUILD_DIR)

clean:
		rm -rf $(BUILD_DIR)/stage1.bin