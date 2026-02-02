# Toolchain & Configuration
CROSS_COMPILE ?= riscv64-unknown-elf-
CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

# Directories
SRC_DIR   = src
INC_DIR   = include
BUILD_DIR = build
SDCARD_DIR = /media/kylelin/opirv2/

# Hardware Connection
TTY      ?= /dev/ttyUSB0
BAUDRATE ?= 115200

# Base Addresses
QEMU_BASE = 0x80200000
OPI_BASE  = 0x00200000
CFLAGS    = -Wall -Wextra -O0 -g -mcmodel=medany -ffreestanding -nostdlib -I$(INC_DIR)

# --- Source Discovery ---
# Find all .c, .S (uppercase), and .s (lowercase) files
SRCS_C   = $(wildcard $(SRC_DIR)/*.c)
SRCS_ASM = $(wildcard $(SRC_DIR)/*.S $(SRC_DIR)/*.s)

# --- Object File Generation ---
# 1. Convert src/*.c -> build/*.o
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS_C))
# 2. Convert src/*.S -> build/*.o (Handle boot.S)
OBJS += $(patsubst $(SRC_DIR)/%.S, $(BUILD_DIR)/%.o, $(filter %.S, $(SRCS_ASM)))
# 3. Convert src/*.s -> build/*.o (Handle lowercase .s if any)
OBJS += $(patsubst $(SRC_DIR)/%.s, $(BUILD_DIR)/%.o, $(filter %.s, $(SRCS_ASM)))

# Default Target
all: image

# --- Compilation Rules ---

# Rule for uppercase .S (needs C preprocessor)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Rule for lowercase .s
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Rule for .c
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# --- Targets ---

# 1. QEMU (Debug)
qemu: CFLAGS += -D__QEMU__
qemu: clean $(OBJS)
	@echo "Building for QEMU (Base: $(QEMU_BASE))..."
	# Generate Linker Script with QEMU Address
	$(CC) -E -P -x c -DKERNEL_BASE=$(QEMU_BASE) $(SRC_DIR)/kernel.ld -o $(BUILD_DIR)/kernel.ld
	# Link
	$(CC) $(CFLAGS) -T $(BUILD_DIR)/kernel.ld -o $(BUILD_DIR)/kernel.elf $(OBJS)
	# Run
	qemu-system-riscv64 -machine virt -bios default -nographic -serial mon:stdio -kernel $(BUILD_DIR)/kernel.elf

# 2. Image (Hardware)
image: CFLAGS += -D__OPI__
image: clean $(OBJS)
	@echo "Building for Orange Pi RV 2 (Base: $(OPI_BASE))..."
	# Generate Linker Script with OPI Address
	$(CC) -E -P -x c -DKERNEL_BASE=$(OPI_BASE) $(SRC_DIR)/kernel.ld -o $(BUILD_DIR)/kernel.ld
	# Link
	$(CC) $(CFLAGS) -T $(BUILD_DIR)/kernel.ld -o $(BUILD_DIR)/kernel.elf $(OBJS)
	# Binary
	$(OBJCOPY) -O binary $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/kernel.bin
	# FIT Image
	@echo "Generating FIT Image..."
	cp $(SRC_DIR)/kernel.its $(BUILD_DIR)/
	cp $(SRC_DIR)/x1_orangepi-rv2.dtb $(BUILD_DIR)/
	cd $(BUILD_DIR) && mkimage -f kernel.its kernel.fit
	@echo "Done: $(BUILD_DIR)/kernel.fit"
	
	# --- MODIFIED SECTION START ---
	@echo "Copy: $(BUILD_DIR)/kernel.fit to $(SDCARD_DIR)/kernel.fit"
	sudo cp $(BUILD_DIR)/kernel.fit $(SDCARD_DIR)
	
	@echo "Syncing data to disk (don't remove yet)..."
	sync
	
	@echo "Unmounting $(SDCARD_DIR)..."
	sudo umount $(SDCARD_DIR)
	
	@echo "Success! Safe to eject SD card."
screen:
	sudo screen /dev/ttyUSB0 115200
clean:
	rm -rf $(BUILD_DIR)