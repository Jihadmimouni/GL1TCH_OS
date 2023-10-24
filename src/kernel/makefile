BUILD_DIR?=build
ASM?=nasm

.PHONY:  all kernel always clean 

all: always kernel

kernel: $(BUILD_DIR)/kernel.bin always

$(BUILD_DIR)/kernel.bin:
		$(ASM) kernel.asm -f bin -o $(BUILD_DIR)/kernel.bin

always:
		mkdir -p $(BUILD_DIR)

clean:
		rm -rf $(BUILD_DIR)/kernel.bin