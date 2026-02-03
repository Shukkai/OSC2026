# Toolchain & Configuration
CROSS_COMPILE ?= riscv64-unknown-elf-
CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

# Directories
SRC_DIR   = src
INC_DIR   = include
BUILD_DIR = build
SDCARD_DIR = /media/kylelin/opirv2/
# Point to your existing rootfs folder
ROOTFS_DIR = rootfs
# Directory for user programs
USER_DIR   = user_program

# Base Addresses
QEMU_BASE = 0x80200000
OPI_BASE  = 0x00200000

# --- Automatic Payload Base Selection ---
# If "qemu" or "qemu_payload" is in the make targets, use High RAM.
# Otherwise (e.g. "make image"), use the Hardware safe offset.
ifneq (,$(filter qemu qemu_payload,$(MAKECMDGOALS)))
    PAYLOAD_BASE = 0x80200000
else
    PAYLOAD_BASE = 0x00200000
endif

# Flags
CFLAGS    = -Wall -Wextra -O0 -g -mcmodel=medany -ffreestanding -nostdlib -I$(INC_DIR) -fomit-frame-pointer -march=rv64gc
# -march=rv64gc_zicbom

# Source Discovery
SRCS_C   = $(wildcard $(SRC_DIR)/*.c)
SRCS_ASM = $(wildcard $(SRC_DIR)/*.S $(SRC_DIR)/*.s)

# Object Generation
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS_C))
OBJS += $(patsubst $(SRC_DIR)/%.S, $(BUILD_DIR)/%.o, $(filter %.S, $(SRCS_ASM)))
OBJS += $(patsubst $(SRC_DIR)/%.s, $(BUILD_DIR)/%.o, $(filter %.s, $(SRCS_ASM)))

all: image

# --- Rules ---
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# --- Targets ---
# Compiles user_program/test.S -> build/test.o -> build/test.elf -> rootfs/user_prog
user_prog:
	@mkdir -p $(BUILD_DIR)
	@echo "Building User Program..."
	# 1. Compile assembly to object
	$(CC) $(CFLAGS) -c $(USER_DIR)/test.S -o $(BUILD_DIR)/test.o
	# 2. Link to address 0x0 (Flat Binary for loading)
	$(CC) -Ttext 0x0 -nostdlib -o $(BUILD_DIR)/test.elf $(BUILD_DIR)/test.o
	# 3. Extract binary and copy to rootfs
	$(OBJCOPY) -O binary $(BUILD_DIR)/test.elf $(ROOTFS_DIR)/user_prog
	@echo "User program installed: $(ROOTFS_DIR)/user_prog"

# Creates initramfs.cpio from your existing rootfs/ directory
cpio: user_prog
	@mkdir -p $(BUILD_DIR)
	@echo "Packing rootfs..."
	@cd $(ROOTFS_DIR) && find . | cpio -o -H newc > ../$(BUILD_DIR)/initramfs.cpio
	@echo "Generated $(BUILD_DIR)/initramfs.cpio"

# Standard Hardware Payload
payload: CFLAGS += -D__OPI__
payload: clean $(OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Building Payload (Hardware Version)..."
	$(CC) -E -P -x c -DKERNEL_BASE=$(PAYLOAD_BASE) $(SRC_DIR)/kernel.ld -o $(BUILD_DIR)/payload.ld
	$(CC) $(CFLAGS) -T $(BUILD_DIR)/payload.ld -o $(BUILD_DIR)/payload.elf $(OBJS)
	$(OBJCOPY) -O binary $(BUILD_DIR)/payload.elf $(BUILD_DIR)/payload.bin
	@echo "Generated Hardware Payload: $(BUILD_DIR)/payload.bin at $(PAYLOAD_BASE)"

# NEW TARGET: QEMU Payload Only (No Run)
qemu_payload: CFLAGS += -D__QEMU__
qemu_payload: clean $(OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Building Payload (QEMU Version)..."
# Generate linker script using QEMU Base
	$(CC) -E -P -x c -DKERNEL_BASE=$(PAYLOAD_BASE) $(SRC_DIR)/kernel.ld -o $(BUILD_DIR)/payload.ld
# Link
	$(CC) $(CFLAGS) -T $(BUILD_DIR)/payload.ld -o $(BUILD_DIR)/payload.elf $(OBJS)
# Binary
	$(OBJCOPY) -O binary $(BUILD_DIR)/payload.elf $(BUILD_DIR)/payload.bin
	@echo "-------------------------------------------------------"
	@echo "QEMU Payload Generated: $(BUILD_DIR)/payload.bin"
	@echo "Load Address:           $(PAYLOAD_BASE)"
	@echo "Ready for transfer script."
	@echo "-------------------------------------------------------"

# 1. QEMU Run (Full System)
qemu: CFLAGS += -D__QEMU__
qemu: clean cpio qemu_payload $(OBJS)
	@echo "Building Kernel ELF for QEMU (Direct Run)..."
# Re-link for the kernel elf used by QEMU command (can reuse payload logic or separate)
# This part creates the 'kernel.elf' QEMU loads initially
	$(CC) -E -P -x c -DKERNEL_BASE=$(QEMU_BASE) $(SRC_DIR)/kernel.ld -o $(BUILD_DIR)/kernel.ld
	$(CC) $(CFLAGS) -T $(BUILD_DIR)/kernel.ld -o $(BUILD_DIR)/kernel.elf $(OBJS)
	
	@echo "-------------------------------------------------------"
	@echo "NOTE: QEMU will pause and wait for a connection."
	@echo "Look for 'char device redirected to /dev/pts/X' below."
	@echo "-------------------------------------------------------"
	qemu-system-riscv64 -machine virt -bios default -nographic -serial pty \
		-kernel $(BUILD_DIR)/kernel.elf \
		-initrd $(BUILD_DIR)/initramfs.cpio \
		-device ramfb 

# 2. Hardware Image
image: CFLAGS += -D__OPI__
image: clean cpio payload $(OBJS)
	@echo "Building for Orange Pi RV 2..."
	$(CC) -E -P -x c -DKERNEL_BASE=$(OPI_BASE) $(SRC_DIR)/kernel.ld -o $(BUILD_DIR)/kernel.ld
	$(CC) $(CFLAGS) -T $(BUILD_DIR)/kernel.ld -o $(BUILD_DIR)/kernel.elf $(OBJS)
	$(OBJCOPY) -O binary $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/kernel.bin
	
	cp $(SRC_DIR)/kernel.its $(BUILD_DIR)/
	cp $(SRC_DIR)/x1_orangepi-rv2.dtb $(BUILD_DIR)/
	
	cd $(BUILD_DIR) && mkimage -f kernel.its kernel.fit
	
	@echo "Build Complete: $(BUILD_DIR)/kernel.fit"
	@echo "Copy: $(BUILD_DIR)/kernel.fit to $(SDCARD_DIR)/kernel.fit"
	sudo cp $(BUILD_DIR)/kernel.fit $(SDCARD_DIR)
	sync
	sudo umount $(SDCARD_DIR)
	@echo "Success! Safe to eject SD card."

screen:
	sudo screen /dev/ttyUSB0 115200

clean:
	rm -rf $(BUILD_DIR) *.dtb