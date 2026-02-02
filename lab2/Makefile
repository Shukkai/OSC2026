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

# Base Addresses
QEMU_BASE = 0x80200000
OPI_BASE  = 0x00200000
# --- Automatic Payload Base Selection ---
# If "qemu" is one of the make targets (e.g. "make qemu"), use High RAM.
# Otherwise (e.g. "make image"), use the Hardware safe offset.
ifeq ($(findstring qemu,$(MAKECMDGOALS)),qemu)
    PAYLOAD_BASE = 0x80200000
else
    PAYLOAD_BASE = 0x00200000
endif
# Flags (Position Independent Code (PIC) so the code uses relative jumps instead of absolute addresses.)
CFLAGS    = -Wall -Wextra -O0 -g -mcmodel=medany -ffreestanding -nostdlib -I$(INC_DIR)
# -fPIC

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
# --- RootFS Target (New) ---
# Creates initramfs.cpio from your existing rootfs/ directory
cpio:
	@mkdir -p $(BUILD_DIR)
	@echo "Packing rootfs..."
	# Go into rootfs folder, find all files, and pipe to cpio
	@cd $(ROOTFS_DIR) && find . | cpio -o -H newc > ../$(BUILD_DIR)/initramfs.cpio
	@echo "Generated $(BUILD_DIR)/initramfs.cpio"

payload: CFLAGS += -D__OPI__
payload: $(OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Building Payload (Kernel to send over UART)..."
	
	# Generate a specific linker script for the payload address
	$(CC) -E -P -x c -DKERNEL_BASE=$(PAYLOAD_BASE) $(SRC_DIR)/kernel.ld -o $(BUILD_DIR)/payload.ld
	
	# Link using the payload linker script
	$(CC) $(CFLAGS) -T $(BUILD_DIR)/payload.ld -o $(BUILD_DIR)/payload.elf $(OBJS)
	
	# Create the binary
	$(OBJCOPY) -O binary $(BUILD_DIR)/payload.elf $(BUILD_DIR)/payload.bin
	
	@echo "-------------------------------------------------------"
	@echo "Payload Generated: $(BUILD_DIR)/payload.bin"
	@echo "Load Address:      $(PAYLOAD_BASE)"
	@echo "Use this file with your python sender script!"
	@echo "-------------------------------------------------------"

# 1. QEMU Run
qemu: CFLAGS += -D__QEMU__
qemu: clean cpio payload $(OBJS)
	@echo "Building for QEMU..."
	$(CC) -E -P -x c -DKERNEL_BASE=$(QEMU_BASE) $(SRC_DIR)/kernel.ld -o $(BUILD_DIR)/kernel.ld
	$(CC) $(CFLAGS) -T $(BUILD_DIR)/kernel.ld -o $(BUILD_DIR)/kernel.elf $(OBJS)
	@echo "-------------------------------------------------------"
	@echo "NOTE: QEMU will pause and wait for a connection."
	@echo "Look for 'char device redirected to /dev/pts/X' below."
	@echo "Then open a new terminal and run: screen /dev/pts/X"
	@echo "-------------------------------------------------------"
	# Changed -serial mon:stdio to -serial pty
	qemu-system-riscv64 -machine virt -bios default -nographic -serial pty \
		-kernel $(BUILD_DIR)/kernel.elf \
		-initrd $(BUILD_DIR)/initramfs.cpio

# 2. Hardware Image
# Added dependency: rootfs_cpio
image: CFLAGS += -D__OPI__
image: clean cpio payload $(OBJS)
	@echo "Building for Orange Pi RV 2..."
	$(CC) -E -P -x c -DKERNEL_BASE=$(OPI_BASE) $(SRC_DIR)/kernel.ld -o $(BUILD_DIR)/kernel.ld
	$(CC) $(CFLAGS) -T $(BUILD_DIR)/kernel.ld -o $(BUILD_DIR)/kernel.elf $(OBJS)
	$(OBJCOPY) -O binary $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/kernel.bin
	
	# Prepare files for mkimage
	cp $(SRC_DIR)/kernel.its $(BUILD_DIR)/
	cp $(SRC_DIR)/x1_orangepi-rv2.dtb $(BUILD_DIR)/
	# Note: .cpio is already in $(BUILD_DIR) from rootfs_cpio target
	
	# Generate FIT Image
	cd $(BUILD_DIR) && mkimage -f kernel.its kernel.fit
	
	@echo "Build Complete: $(BUILD_DIR)/kernel.fit"
	@echo "Copy: $(BUILD_DIR)/kernel.fit to $(SDCARD_DIR)/kernel.fit"
	sudo cp $(BUILD_DIR)/kernel.fit $(SDCARD_DIR)
	@echo "Syncing data to disk..."
	sync
	@echo "Unmounting $(SDCARD_DIR)..."
	sudo umount $(SDCARD_DIR)
	@echo "Success! Safe to eject SD card."

screen:
	sudo screen /dev/ttyUSB0 115200



clean:
	rm -rf $(BUILD_DIR) *.dtb