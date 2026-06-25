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
# 95000 covers the userland library migration (libkarm pulls in
# crt0 + syscall + string per migrated app, which adds ~1 KB each).
KERNEL_SIZE_LIMIT ?= 95000
APPS := shell editor monitor clock panel
APPS_DIR := programs/apps
APPS_COMMON_OBJ := $(BUILD_DIR)/$(APPS_DIR)/common.o
APP_OBJS := $(addprefix $(BUILD_DIR)/$(APPS_DIR)/,$(addsuffix .o,$(APPS)))
APP_ELFS := $(addprefix $(BUILD_DIR)/$(APPS_DIR)/,$(addsuffix .elf,$(APPS)))
APP_BINS := $(addprefix $(BUILD_DIR)/$(APPS_DIR)/,$(addsuffix .bin,$(APPS)))
APP_BLOBS := $(addprefix $(BUILD_DIR)/$(APPS_DIR)/,$(addsuffix _blob.o,$(APPS)))
VIRTIO_BLK_IMG := $(BUILD_DIR)/virtio-blk.img
MKFAT32_IMAGE := $(BUILD_DIR)/tools/mkfat32_image

# programs/libkarm — userland support library (syscall wrappers, crt0,
# string helpers). The window/compositor wrappers in
# programs/libkarmdesk are built only after every app is on libkarm;
# their objects are intentionally not part of LIBKARM_OBJS yet.
LIBKARM_DIR := programs/libkarm
LIBKARM_OBJS := \
    $(BUILD_DIR)/$(LIBKARM_DIR)/syscall.o \
    $(BUILD_DIR)/$(LIBKARM_DIR)/crt0.o \
    $(BUILD_DIR)/$(LIBKARM_DIR)/string.o

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
           -mcpu=cortex-a72 -std=c11 -Os -g -I . -I drivers
# Userland C compiles with the same flags plus the libkarm include path
# so app code can pull in <syscall.h>, <errno.h>, <string.h> from its
# own build unit without an explicit relative include.
USERLAND_CFLAGS := $(CFLAGS) -I programs -I $(LIBKARM_DIR)
USERLAND_ASFLAGS := $(ASFLAGS) -I programs -I $(LIBKARM_DIR)

OBJS := \
    $(BUILD_DIR)/boot/start.o \
    $(BUILD_DIR)/$(BOARD_DIR)/board.o \
    $(BUILD_DIR)/drivers/fb/fb.o \
    $(BUILD_DIR)/drivers/gpu/virtio_gpu.o \
    $(BUILD_DIR)/drivers/irq/gicv2.o \
    $(BUILD_DIR)/drivers/net/virtio_net.o \
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
    $(BUILD_DIR)/kernel/net/dhcp.o \
    $(BUILD_DIR)/kernel/print.o \
    $(BUILD_DIR)/kernel/process.o \
    $(BUILD_DIR)/kernel/sched/sched.o \
    $(BUILD_DIR)/kernel/sched/switch.o \
    $(BUILD_DIR)/kernel/syscall.o \
    $(BUILD_DIR)/kernel/timer/timer.o \
    $(BUILD_DIR)/kernel/tmpfs.o \
    $(BUILD_DIR)/kernel/panel_boot.o \
    $(BUILD_DIR)/kernel/panel_boot_recovery.o \
    $(BUILD_DIR)/kernel/user_entry.o \
    $(BUILD_DIR)/kernel/user_image.o \
    $(BUILD_DIR)/kernel/user_vm.o \
    $(BUILD_DIR)/kernel/vfs.o \
    $(APP_BLOBS) \
    $(BUILD_DIR)/drivers/uart/pl011.o \
    $(BUILD_DIR)/drivers/input/input.o \
    $(BUILD_DIR)/drivers/input/virtio_input.o \
    $(BUILD_DIR)/drivers/pci/pci.o \
    $(BUILD_DIR)/drivers/usb/hid.o \
    $(BUILD_DIR)/drivers/usb/uhci.o \
    $(BUILD_DIR)/drivers/usb/usb_core.o \
    $(BUILD_DIR)/drivers/usb/hid_driver.o

DEPS := $(OBJS:.o=.d)

.PHONY: all toolchain-check qemu-check qemu qemu-blk qemu-fb qemu-fb-visible qemu-debug qemu-net qemu-usb entry-check size clean apps libkarm

all: toolchain-check $(KERNEL_ELF) $(KERNEL_BIN)

apps: $(APP_ELFS) $(APP_BINS)

# libkarm is built standalone so its objects can be linked into any
# userland app that has been migrated. Until an app's Makefile rule
# is updated to depend on $(LIBKARM_OBJS), the app still links
# against programs/apps/common.o and uses inline `svc #0`.
libkarm: $(LIBKARM_OBJS)

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

# Userland C apps. Override the generic C rule so they pick up the
# libkarm include path through USERLAND_CFLAGS.
$(BUILD_DIR)/$(APPS_DIR)/%.o: $(APPS_DIR)/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(DEPFLAGS) $(USERLAND_CFLAGS) -c $< -o $@

# programs/libkarm — userland support library. These rules override the
# generic `$(BUILD_DIR)/%.o` patterns so userland code gets the
# USERLAND_* flags (which add programs/libkarm to the include path).
$(BUILD_DIR)/$(LIBKARM_DIR)/%.o: $(LIBKARM_DIR)/%.S | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(DEPFLAGS) $(USERLAND_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/$(LIBKARM_DIR)/%.o: $(LIBKARM_DIR)/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(DEPFLAGS) $(USERLAND_CFLAGS) -c $< -o $@

# apps that have migrated to libkarm link against the libkarm objects
# directly instead of programs/apps/common.o. clock and monitor are
# the first two; the rules below keep their dependency lists
# explicit so the generic pattern rule (which still pulls in common.o
# for the rest of the apps) does not fire for them. *_end.o is
# linked last so the *_image_end marker sits at the tail of the flat
# image.
$(BUILD_DIR)/$(APPS_DIR)/clock.elf: $(BUILD_DIR)/$(APPS_DIR)/clock.o \
    $(BUILD_DIR)/$(APPS_DIR)/clock_header.o \
    $(BUILD_DIR)/$(LIBKARM_DIR)/syscall.o \
    $(BUILD_DIR)/$(LIBKARM_DIR)/crt0.o \
    $(BUILD_DIR)/$(APPS_DIR)/clock_end.o \
    $(APPS_DIR)/image.ld
	$(LD) -T $(APPS_DIR)/image.ld -nostdlib \
	    $(BUILD_DIR)/$(APPS_DIR)/clock.o \
	    $(BUILD_DIR)/$(APPS_DIR)/clock_header.o \
	    $(BUILD_DIR)/$(LIBKARM_DIR)/syscall.o \
	    $(BUILD_DIR)/$(LIBKARM_DIR)/crt0.o \
	    $(BUILD_DIR)/$(APPS_DIR)/clock_end.o \
	    -o $@

$(BUILD_DIR)/$(APPS_DIR)/monitor.elf: $(BUILD_DIR)/$(APPS_DIR)/monitor.o \
    $(BUILD_DIR)/$(APPS_DIR)/monitor_header.o \
    $(BUILD_DIR)/$(LIBKARM_DIR)/syscall.o \
    $(BUILD_DIR)/$(LIBKARM_DIR)/crt0.o \
    $(BUILD_DIR)/$(LIBKARM_DIR)/string.o \
    $(BUILD_DIR)/$(APPS_DIR)/monitor_end.o \
    $(APPS_DIR)/image.ld
	$(LD) -T $(APPS_DIR)/image.ld -nostdlib \
	    $(BUILD_DIR)/$(APPS_DIR)/monitor.o \
	    $(BUILD_DIR)/$(APPS_DIR)/monitor_header.o \
	    $(BUILD_DIR)/$(LIBKARM_DIR)/syscall.o \
	    $(BUILD_DIR)/$(LIBKARM_DIR)/crt0.o \
	    $(BUILD_DIR)/$(LIBKARM_DIR)/string.o \
	    $(BUILD_DIR)/$(APPS_DIR)/monitor_end.o \
	    -o $@

$(BUILD_DIR)/$(APPS_DIR)/editor.elf: $(BUILD_DIR)/$(APPS_DIR)/editor.o \
    $(BUILD_DIR)/$(APPS_DIR)/editor_header.o \
    $(BUILD_DIR)/$(LIBKARM_DIR)/syscall.o \
    $(BUILD_DIR)/$(LIBKARM_DIR)/crt0.o \
    $(BUILD_DIR)/$(APPS_DIR)/editor_end.o \
    $(APPS_DIR)/image.ld
	$(LD) -T $(APPS_DIR)/image.ld -nostdlib \
	    $(BUILD_DIR)/$(APPS_DIR)/editor.o \
	    $(BUILD_DIR)/$(APPS_DIR)/editor_header.o \
	    $(BUILD_DIR)/$(LIBKARM_DIR)/syscall.o \
	    $(BUILD_DIR)/$(LIBKARM_DIR)/crt0.o \
	    $(BUILD_DIR)/$(APPS_DIR)/editor_end.o \
	    -o $@

$(BUILD_DIR)/$(APPS_DIR)/shell.elf: $(BUILD_DIR)/$(APPS_DIR)/shell.o \
    $(BUILD_DIR)/$(APPS_DIR)/shell_header.o \
    $(BUILD_DIR)/$(LIBKARM_DIR)/syscall.o \
    $(BUILD_DIR)/$(LIBKARM_DIR)/crt0.o \
    $(BUILD_DIR)/$(LIBKARM_DIR)/string.o \
    $(BUILD_DIR)/$(APPS_DIR)/shell_end.o \
    $(APPS_DIR)/image.ld
	$(LD) -T $(APPS_DIR)/image.ld -nostdlib \
	    $(BUILD_DIR)/$(APPS_DIR)/shell.o \
	    $(BUILD_DIR)/$(APPS_DIR)/shell_header.o \
	    $(BUILD_DIR)/$(LIBKARM_DIR)/syscall.o \
	    $(BUILD_DIR)/$(LIBKARM_DIR)/crt0.o \
	    $(BUILD_DIR)/$(LIBKARM_DIR)/string.o \
	    $(BUILD_DIR)/$(APPS_DIR)/shell_end.o \
	    -o $@

$(BUILD_DIR)/$(APPS_DIR)/panel.elf: $(BUILD_DIR)/$(APPS_DIR)/panel.o \
    $(BUILD_DIR)/$(APPS_DIR)/panel_header.o \
    $(BUILD_DIR)/$(LIBKARM_DIR)/syscall.o \
    $(BUILD_DIR)/$(LIBKARM_DIR)/crt0.o \
    $(BUILD_DIR)/$(LIBKARM_DIR)/string.o \
    $(BUILD_DIR)/$(APPS_DIR)/panel_end.o \
    $(APPS_DIR)/image.ld
	$(LD) -T $(APPS_DIR)/image.ld -nostdlib \
	    $(BUILD_DIR)/$(APPS_DIR)/panel.o \
	    $(BUILD_DIR)/$(APPS_DIR)/panel_header.o \
	    $(BUILD_DIR)/$(LIBKARM_DIR)/syscall.o \
	    $(BUILD_DIR)/$(LIBKARM_DIR)/crt0.o \
	    $(BUILD_DIR)/$(LIBKARM_DIR)/string.o \
	    $(BUILD_DIR)/$(APPS_DIR)/panel_end.o \
	    -o $@

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

$(VIRTIO_BLK_IMG): $(MKFAT32_IMAGE) $(BUILD_DIR)/$(APPS_DIR)/shell.bin | $(BUILD_DIR)
	$(MKFAT32_IMAGE) $@ $(BUILD_DIR)/$(APPS_DIR)/shell.bin

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
	    -device virtio-gpu-device,xres=640,yres=480 \
	    -device virtio-mouse-device

qemu-debug: qemu-check entry-check $(KERNEL_BIN)
	qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M -nographic \
	    -kernel $(KERNEL_BIN) -S -s

qemu-net: qemu-check entry-check $(KERNEL_BIN)
	qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M -nographic \
	    -netdev user,id=net0 -device virtio-net-device,netdev=net0 \
	    -global virtio-mmio.force-legacy=false \
	    -kernel $(KERNEL_BIN)

qemu-usb: qemu-check entry-check $(KERNEL_BIN)
	qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M -nographic \
	    -global virtio-mmio.force-legacy=false \
	    -kernel $(KERNEL_BIN) \
	    -device piix3-usb-uhci \
	    -device usb-kbd \
	    -device usb-mouse

size: $(KERNEL_ELF) $(KERNEL_BIN)
	$(SIZE) $(KERNEL_ELF)
	@bytes=$$(wc -c < $(KERNEL_BIN)); \
	printf "kernel.bin: %s bytes (limit: $(KERNEL_SIZE_LIMIT))\n" "$$bytes"; \
	test "$$bytes" -lt $(KERNEL_SIZE_LIMIT)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
