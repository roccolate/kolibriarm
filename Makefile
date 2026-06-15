CROSS_COMPILE ?= aarch64-linux-gnu-

CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
READELF := $(CROSS_COMPILE)readelf
SIZE    := $(CROSS_COMPILE)size

BUILD_DIR := build
BOARD ?= qemu_virt
BOARD_DIR := drivers/boards/$(BOARD)
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
USER_DEMO_OBJ := $(BUILD_DIR)/programs/user_demo.o
USER_DEMO_ELF := $(BUILD_DIR)/programs/user_demo.elf
USER_DEMO_BIN := $(BUILD_DIR)/programs/user_demo.bin
USER_DEMO_BLOB_OBJ := $(BUILD_DIR)/programs/user_demo_blob.o
VIRTIO_BLK_IMG := $(BUILD_DIR)/virtio-blk.img

LOAD_ADDR := 0x40080000
LOAD_ADDR_HEX := 40080000

ASFLAGS := -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -mcpu=cortex-a72 -g
CFLAGS  := -Wall -Wextra -Werror -ffreestanding -nostdlib -nostartfiles \
           -fno-builtin -fno-stack-protector -mgeneral-regs-only \
           -mcpu=cortex-a72 -std=c11 -Os -g -I . -I drivers
LDFLAGS := -T linker.ld -nostdlib

OBJS := \
    $(BUILD_DIR)/boot/start.o \
    $(BUILD_DIR)/$(BOARD_DIR)/board.o \
    $(BUILD_DIR)/drivers/fb/fb.o \
    $(BUILD_DIR)/drivers/gpu/virtio_gpu.o \
    $(BUILD_DIR)/drivers/irq/gicv2.o \
    $(BUILD_DIR)/drivers/storage/virtio_blk.o \
    $(BUILD_DIR)/kernel/dtb.o \
    $(BUILD_DIR)/kernel/exception_vectors.o \
    $(BUILD_DIR)/kernel/exceptions.o \
    $(BUILD_DIR)/kernel/irq.o \
    $(BUILD_DIR)/kernel/irq_asm.o \
    $(BUILD_DIR)/kernel/kernel.o \
    $(BUILD_DIR)/kernel/mm/kheap.o \
    $(BUILD_DIR)/kernel/mm/mmu.o \
    $(BUILD_DIR)/kernel/mm/pmm.o \
    $(BUILD_DIR)/kernel/mm/vmm.o \
    $(BUILD_DIR)/kernel/process.o \
    $(BUILD_DIR)/kernel/process_context.o \
    $(BUILD_DIR)/kernel/sched/sched.o \
    $(BUILD_DIR)/kernel/sched/switch.o \
    $(BUILD_DIR)/kernel/syscall.o \
    $(BUILD_DIR)/kernel/timer/timer.o \
    $(BUILD_DIR)/kernel/user_demo.o \
    $(BUILD_DIR)/kernel/user_entry.o \
    $(BUILD_DIR)/kernel/user_image.o \
    $(BUILD_DIR)/kernel/user_vm.o \
    $(USER_DEMO_BLOB_OBJ) \
    $(BUILD_DIR)/drivers/uart/pl011.o

.PHONY: all toolchain-check qemu-check qemu qemu-blk qemu-fb qemu-fb-visible qemu-debug entry-check size clean

all: toolchain-check $(KERNEL_ELF) $(KERNEL_BIN)

toolchain-check:
	@command -v $(CC) >/dev/null 2>&1 || { echo "error: missing $(CC)"; exit 1; }
	@command -v $(LD) >/dev/null 2>&1 || { echo "error: missing $(LD)"; exit 1; }
	@command -v $(OBJCOPY) >/dev/null 2>&1 || { echo "error: missing $(OBJCOPY)"; exit 1; }
	@command -v $(READELF) >/dev/null 2>&1 || { echo "error: missing $(READELF)"; exit 1; }
	@command -v $(SIZE) >/dev/null 2>&1 || { echo "error: missing $(SIZE)"; exit 1; }

qemu-check:
	@command -v qemu-system-aarch64 >/dev/null 2>&1 || \
	    { echo "error: missing qemu-system-aarch64"; exit 1; }

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(USER_DEMO_ELF): $(USER_DEMO_OBJ) programs/user_image.ld
	$(LD) -T programs/user_image.ld -nostdlib $(USER_DEMO_OBJ) -o $@

$(USER_DEMO_BIN): $(USER_DEMO_ELF)
	$(OBJCOPY) -O binary $< $@

$(USER_DEMO_BLOB_OBJ): $(USER_DEMO_BIN)
	$(OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 \
	    --rename-section .data=.user_demo_blob,alloc,load,readonly,data,contents \
	    $< $@

$(KERNEL_ELF): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(VIRTIO_BLK_IMG): | $(BUILD_DIR)
	truncate -s 1M $@

entry-check: $(KERNEL_ELF)
	@$(READELF) -h $(KERNEL_ELF) | grep "Entry point address:" | grep -q "$(LOAD_ADDR)"
	@$(READELF) -s $(KERNEL_ELF) | grep " _start$$" | grep -q "0*$(LOAD_ADDR_HEX)"
	@$(READELF) -s $(KERNEL_ELF) | grep " _start$$"

qemu: qemu-check entry-check $(KERNEL_BIN)
	qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M -nographic \
	    -kernel $(KERNEL_BIN)

qemu-blk: qemu-check entry-check $(KERNEL_BIN) $(VIRTIO_BLK_IMG)
	qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M -nographic \
	    -global virtio-mmio.force-legacy=false \
	    -kernel $(KERNEL_BIN) \
	    -drive file=$(VIRTIO_BLK_IMG),if=none,format=raw,id=hd0 \
	    -device virtio-blk-device,drive=hd0

qemu-fb: qemu-check entry-check $(KERNEL_BIN)
	qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M \
	    -display none -serial stdio \
	    -global virtio-mmio.force-legacy=false \
	    -kernel $(KERNEL_BIN) \
	    -device virtio-gpu-device,xres=640,yres=480

qemu-fb-visible: qemu-check entry-check $(KERNEL_BIN)
	qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M \
	    -display gtk,gl=off -serial stdio \
	    -global virtio-mmio.force-legacy=false \
	    -kernel $(KERNEL_BIN) \
	    -device virtio-gpu-device,xres=640,yres=480

qemu-debug: qemu-check entry-check $(KERNEL_BIN)
	qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M -nographic \
	    -kernel $(KERNEL_BIN) -S -s

size: $(KERNEL_ELF) $(KERNEL_BIN)
	$(SIZE) $(KERNEL_ELF)
	@bytes=$$(wc -c < $(KERNEL_BIN)); \
	printf "kernel.bin: %s bytes\n" "$$bytes"; \
	test "$$bytes" -lt 32768

clean:
	rm -rf $(BUILD_DIR)
