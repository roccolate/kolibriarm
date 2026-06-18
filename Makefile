CROSS_COMPILE ?= aarch64-linux-gnu-
HOST_CC ?= gcc

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
KERNEL_SIZE_LIMIT ?= 71000
APPS := hello loop fault shell editor monitor win panel clock
APPS_DIR := programs/apps
APPS_COMMON_OBJ := $(BUILD_DIR)/$(APPS_DIR)/common.o
APP_OBJS := $(addprefix $(BUILD_DIR)/$(APPS_DIR)/,$(addsuffix .o,$(APPS)))
APP_ELFS := $(addprefix $(BUILD_DIR)/$(APPS_DIR)/,$(addsuffix .elf,$(APPS)))
APP_BINS := $(addprefix $(BUILD_DIR)/$(APPS_DIR)/,$(addsuffix .bin,$(APPS)))
APP_BLOBS := $(addprefix $(BUILD_DIR)/$(APPS_DIR)/,$(addsuffix _blob.o,$(APPS)))
VIRTIO_BLK_IMG := $(BUILD_DIR)/virtio-blk.img
MKFAT32_IMAGE := $(BUILD_DIR)/tools/mkfat32_image

LOAD_ADDR := 0x40080000
LOAD_ADDR_HEX := 40080000
LDFLAGS := -T linker.ld -nostdlib

ifeq ($(BOARD),rpi4)
LOAD_ADDR := 0x80000
LOAD_ADDR_HEX := 80000
LDFLAGS := -T linker_rpi4.ld -nostdlib
STORAGE_DEV := $(BUILD_DIR)/drivers/storage/emmc.o
else
STORAGE_DEV := $(BUILD_DIR)/drivers/storage/virtio_blk.o
endif

DEPFLAGS := -MMD -MP
ASFLAGS := -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -mcpu=cortex-a72 -g
CFLAGS  := -Wall -Wextra -Werror -ffreestanding -nostdlib -nostartfiles \
           -fno-builtin -fno-stack-protector -mgeneral-regs-only \
           -mcpu=cortex-a72 -std=c11 -Os -g -I . -I drivers -I third_party/lwip_port

OBJS := \
    $(BUILD_DIR)/boot/start.o \
    $(BUILD_DIR)/$(BOARD_DIR)/board.o \
    $(BUILD_DIR)/drivers/display/display.o \
    $(BUILD_DIR)/drivers/display/gfx.o \
    $(BUILD_DIR)/drivers/fb/fb.o \
    $(BUILD_DIR)/drivers/gpu/virtio_gpu.o \
    $(BUILD_DIR)/drivers/irq/gicv2.o \
    $(BUILD_DIR)/drivers/net/virtio_net.o \
    $(BUILD_DIR)/third_party/lwip_port/lwip_port.o \
    $(STORAGE_DEV) \
    $(BUILD_DIR)/kernel/boot_program.o \
    $(BUILD_DIR)/kernel/bootfs.o \
    $(BUILD_DIR)/kernel/console.o \
    $(BUILD_DIR)/kernel/dtb.o \
    $(BUILD_DIR)/kernel/fat32.o \
    $(BUILD_DIR)/kernel/font.o \
    $(BUILD_DIR)/kernel/gui.o \
    $(BUILD_DIR)/kernel/ipc.o \
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
    $(BUILD_DIR)/kernel/tmpfs.o \
    $(BUILD_DIR)/kernel/user_demo.o \
    $(BUILD_DIR)/kernel/user_entry.o \
    $(BUILD_DIR)/kernel/user_image.o \
    $(BUILD_DIR)/kernel/user_vm.o \
    $(BUILD_DIR)/kernel/vfs.o \
    $(APP_BLOBS) \
    $(BUILD_DIR)/drivers/uart/pl011.o \
    $(BUILD_DIR)/drivers/input/input.o \
    $(BUILD_DIR)/drivers/input/virtio_input.o \
    $(BUILD_DIR)/drivers/audio/audio.o \
    $(BUILD_DIR)/drivers/audio/virtio_snd.o

DEPS := $(OBJS:.o=.d)

.PHONY: all toolchain-check qemu-check qemu qemu-blk qemu-fb qemu-fb-visible qemu-debug qemu-net entry-check size clean apps

all: toolchain-check $(KERNEL_ELF) $(KERNEL_BIN)

apps: $(APP_ELFS) $(APP_BINS)

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
	$(CC) $(DEPFLAGS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(DEPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(APPS_DIR)/%.o: $(APPS_DIR)/%.S | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(DEPFLAGS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/$(APPS_DIR)/%.elf: $(BUILD_DIR)/$(APPS_DIR)/%.o $(APPS_COMMON_OBJ) $(APPS_DIR)/image.ld
	$(LD) -T $(APPS_DIR)/image.ld -nostdlib \
	    $(BUILD_DIR)/$(APPS_DIR)/$*.o $(APPS_COMMON_OBJ) -o $@

$(BUILD_DIR)/$(APPS_DIR)/%.bin: $(BUILD_DIR)/$(APPS_DIR)/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/$(APPS_DIR)/%_blob.o: $(BUILD_DIR)/$(APPS_DIR)/%.bin
	$(OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 \
	    --rename-section .data=.app_$*_blob,alloc,load,readonly,data,contents \
	    $< $@

$(KERNEL_ELF): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(MKFAT32_IMAGE): tools/mkfat32_image.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(HOST_CC) -Wall -Wextra -O2 $< -o $@

$(VIRTIO_BLK_IMG): $(MKFAT32_IMAGE) $(BUILD_DIR)/$(APPS_DIR)/hello.bin | $(BUILD_DIR)
	$(MKFAT32_IMAGE) $@ $(BUILD_DIR)/$(APPS_DIR)/hello.bin

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

qemu-net: qemu-check entry-check $(KERNEL_BIN)
	qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M -nographic \
	    -netdev user,id=net0 -device virtio-net-device,netdev=net0 \
	    -global virtio-mmio.force-legacy=false \
	    -kernel $(KERNEL_BIN)

size: $(KERNEL_ELF) $(KERNEL_BIN)
	$(SIZE) $(KERNEL_ELF)
	@bytes=$$(wc -c < $(KERNEL_BIN)); \
	printf "kernel.bin: %s bytes (limit: $(KERNEL_SIZE_LIMIT))\n" "$$bytes"; \
	test "$$bytes" -lt $(KERNEL_SIZE_LIMIT)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
