CROSS_COMPILE ?= aarch64-linux-gnu-
HOST_CC ?= gcc

CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
READELF := $(CROSS_COMPILE)readelf
SIZE    := $(CROSS_COMPILE)size

V ?= 0
ifeq ($(V),1)
Q :=
LOG_AS :=
LOG_CC :=
LOG_HOSTCC :=
LOG_LD :=
LOG_OBJCOPY :=
LOG_MKFAT32 :=
LOG_CHECK :=
LOG_QEMU :=
LOG_SIZE :=
else
Q := @
LOG_AS = @printf "  AS      %s\n" "$@";
LOG_CC = @printf "  CC      %s\n" "$@";
LOG_HOSTCC = @printf "  HOSTCC  %s\n" "$@";
LOG_LD = @printf "  LD      %s\n" "$@";
LOG_OBJCOPY = @printf "  OBJCOPY %s\n" "$@";
LOG_MKFAT32 = @printf "  MKFAT32 %s\n" "$@";
LOG_CHECK = @printf "  CHECK   %s\n" "$@";
LOG_QEMU = @printf "  QEMU    %s\n" "$@";
LOG_SIZE = @printf "  SIZE    %s\n" "$<";
endif

BUILD_DIR := build
BOARD ?= qemu_virt
BOARD_DIR := drivers/boards/$(BOARD)
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
# 100 KB covers the libkarm-migrated apps plus the xHCI command/event
# ring backend while keeping the kernel binary under a tight ceiling.
KERNEL_SIZE_LIMIT ?= 100000
APPS := shell editor monitor clock panel
APPS_DIR := programs/apps
APP_OBJS := $(addprefix $(BUILD_DIR)/$(APPS_DIR)/,$(addsuffix .o,$(APPS)))
APP_HEADER_OBJS := $(addprefix $(BUILD_DIR)/$(APPS_DIR)/,$(addsuffix _header.o,$(APPS)))
APP_END_OBJS := $(addprefix $(BUILD_DIR)/$(APPS_DIR)/,$(addsuffix _end.o,$(APPS)))
APP_IMAGE_OBJS := $(APP_OBJS) $(APP_HEADER_OBJS) $(APP_END_OBJS)
APP_ELFS := $(addprefix $(BUILD_DIR)/$(APPS_DIR)/,$(addsuffix .elf,$(APPS)))
APP_BINS := $(addprefix $(BUILD_DIR)/$(APPS_DIR)/,$(addsuffix .bin,$(APPS)))
APP_BLOBS := $(addprefix $(BUILD_DIR)/$(APPS_DIR)/,$(addsuffix _blob.o,$(APPS)))
VIRTIO_BLK_IMG := $(BUILD_DIR)/virtio-blk.img
MKFAT32_IMAGE := $(BUILD_DIR)/tools/mkfat32_image
QEMU_FS_TEST_LOG := $(BUILD_DIR)/qemu-fs-test.log
QEMU_FS_TEST_TIMEOUT ?= 25s

# programs/libkarm is the freestanding userland support library:
# syscall trampolines, crt0, and small string/number helpers. The
# libkarmdesk window wrappers are header-only and compile into each app.
LIBKARM_DIR := programs/libkarm
LIBKARM_SYSCALL_OBJ := $(BUILD_DIR)/$(LIBKARM_DIR)/syscall.o
LIBKARM_CRT0_OBJ := $(BUILD_DIR)/$(LIBKARM_DIR)/crt0.o
LIBKARM_STRING_OBJ := $(BUILD_DIR)/$(LIBKARM_DIR)/string.o
LIBKARM_OBJS := \
    $(LIBKARM_SYSCALL_OBJ) \
    $(LIBKARM_CRT0_OBJ) \
    $(LIBKARM_STRING_OBJ)

# Per-app libkarm dependencies. Keep string.o out of apps that do not
# use it; those bytes are copied into the embedded bootfs image.
APP_LIBS_clock :=
APP_LIBS_editor :=
APP_LIBS_monitor := $(LIBKARM_STRING_OBJ)
APP_LIBS_panel := $(LIBKARM_STRING_OBJ)
APP_LIBS_shell := $(LIBKARM_STRING_OBJ)

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
USERLAND_EXTRA_CFLAGS ?=
USERLAND_CFLAGS := $(CFLAGS) -I programs -I $(LIBKARM_DIR) \
                   $(USERLAND_EXTRA_CFLAGS)
USERLAND_ASFLAGS := $(ASFLAGS) -I programs -I $(LIBKARM_DIR)
APP_STACK_CHECK_BUILD_DIR ?= build-stack-check
APP_STACK_LIMIT ?= 3072

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
    $(BUILD_DIR)/kernel/gui_backing.o \
    $(BUILD_DIR)/kernel/gui_cursor.o \
    $(BUILD_DIR)/kernel/gui_events.o \
    $(BUILD_DIR)/kernel/gui_input.o \
    $(BUILD_DIR)/kernel/gui_pool.o \
    $(BUILD_DIR)/kernel/gui_compositor.o \
    $(BUILD_DIR)/kernel/init_status.o \
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
    $(BUILD_DIR)/kernel/net/dhcp_options.o \
    $(BUILD_DIR)/kernel/print.o \
    $(BUILD_DIR)/kernel/process.o \
    $(BUILD_DIR)/kernel/sched/sched.o \
    $(BUILD_DIR)/kernel/sched/switch.o \
    $(BUILD_DIR)/kernel/syscall_helpers.o \
    $(BUILD_DIR)/kernel/syscall.o \
    $(BUILD_DIR)/kernel/timer/timer.o \
    $(BUILD_DIR)/kernel/tmpfs.o \
    $(BUILD_DIR)/kernel/panel_boot_argv.o \
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
    $(BUILD_DIR)/drivers/usb/xhci.o \
    $(BUILD_DIR)/drivers/usb/usb_core.o \
    $(BUILD_DIR)/drivers/usb/hid_driver.o

DEPS := $(OBJS:.o=.d) $(APP_IMAGE_OBJS:.o=.d) $(LIBKARM_OBJS:.o=.d)

.SECONDARY: $(APP_IMAGE_OBJS)

.PHONY: all help toolchain-check qemu-check qemu qemu-blk qemu-fs-test qemu-fb qemu-fb-visible qemu-debug qemu-net qemu-usb entry-check size clean apps libkarm stack-check

all: toolchain-check $(KERNEL_ELF) $(KERNEL_BIN)

help:
	@printf "KolibriARM make targets:\n"
	@printf "  %-18s %s\n" "qemu" "run the serial QEMU target"
	@printf "  %-18s %s\n" "qemu-debug" "run QEMU paused with a GDB server"
	@printf "  %-18s %s\n" "qemu-blk" "run QEMU with a generated FAT32 virtio-blk image"
	@printf "  %-18s %s\n" "qemu-fs-test" "smoke-test FAT32 storage wiring in QEMU"
	@printf "  %-18s %s\n" "qemu-fb" "run QEMU headless with virtio-gpu"
	@printf "  %-18s %s\n" "qemu-fb-visible" "run QEMU with visible virtio-gpu and mouse input"
	@printf "  %-18s %s\n" "qemu-net" "run QEMU with virtio-net"
	@printf "  %-18s %s\n" "qemu-usb" "run QEMU with xHCI USB keyboard and mouse"
	@printf "  %-18s %s\n" "size" "print kernel ELF and binary size"
	@printf "  %-18s %s\n" "libkarm" "build freestanding userland support objects"
	@printf "  %-18s %s\n" "apps" "build userland app ELF and binary images"
	@printf "  %-18s %s\n" "stack-check" "measure userland C stack usage"
	@printf "  %-18s %s\n" "toolchain-check" "check required cross-toolchain programs"
	@printf "  %-18s %s\n" "qemu-check" "check qemu-system-aarch64 is available"
	@printf "  %-18s %s\n" "clean" "remove the build directory"
	@printf "  %-18s %s\n" "entry-check" "verify the kernel entry address"
	@printf "\nOptions:\n"
	@printf "  %-18s %s\n" "BOARD=qemu_virt" "select the board build"
	@printf "  %-18s %s\n" "V=1" "show full build commands"
	@printf "  %-18s %s\n" "APP_STACK_LIMIT=n" "set stack-check byte threshold"

apps: $(APP_ELFS) $(APP_BINS)

# Build the standalone userland objects. Apps link the subset they use
# explicitly below so small apps do not pull in string helpers.
libkarm: $(LIBKARM_OBJS)

stack-check:
	@$(MAKE) -B --no-print-directory BUILD_DIR=$(APP_STACK_CHECK_BUILD_DIR) \
	    USERLAND_EXTRA_CFLAGS="$(USERLAND_EXTRA_CFLAGS) -fstack-usage" apps
	@awk -v limit="$(APP_STACK_LIMIT)" ' \
	    { \
	        bytes = $$2 + 0; \
	        if (bytes > max) { max = bytes; max_fn = $$1; } \
	        if (bytes > limit) { \
	            printf "stack-check: %s uses %s bytes, limit %s\n", \
	                   $$1, $$2, limit; \
	            bad = 1; \
	        } \
	    } \
	    END { \
	        if (max_fn != "") { \
	            printf "stack-check: max %s bytes in %s (limit %s)\n", \
	                   max, max_fn, limit; \
	        } \
	        exit bad; \
	    }' \
	    $(APP_STACK_CHECK_BUILD_DIR)/$(APPS_DIR)/*.su \
	    $(APP_STACK_CHECK_BUILD_DIR)/$(LIBKARM_DIR)/*.su

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
	$(Q)mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	$(Q)mkdir -p $(dir $@)
	$(LOG_AS)$(CC) $(DEPFLAGS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(Q)mkdir -p $(dir $@)
	$(LOG_CC)$(CC) $(DEPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(APPS_DIR)/%.o: $(APPS_DIR)/%.S | $(BUILD_DIR)
	$(Q)mkdir -p $(dir $@)
	$(LOG_AS)$(CC) $(DEPFLAGS) $(ASFLAGS) -c $< -o $@

# Userland C apps. Override the generic C rule so they pick up the
# libkarm include path through USERLAND_CFLAGS.
$(BUILD_DIR)/$(APPS_DIR)/%.o: $(APPS_DIR)/%.c | $(BUILD_DIR)
	$(Q)mkdir -p $(dir $@)
	$(LOG_CC)$(CC) $(DEPFLAGS) $(USERLAND_CFLAGS) -c $< -o $@

# programs/libkarm — userland support library. These rules override the
# generic `$(BUILD_DIR)/%.o` patterns so userland code gets the
# USERLAND_* flags (which add programs/libkarm to the include path).
$(BUILD_DIR)/$(LIBKARM_DIR)/%.o: $(LIBKARM_DIR)/%.S | $(BUILD_DIR)
	$(Q)mkdir -p $(dir $@)
	$(LOG_AS)$(CC) $(DEPFLAGS) $(USERLAND_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/$(LIBKARM_DIR)/%.o: $(LIBKARM_DIR)/%.c | $(BUILD_DIR)
	$(Q)mkdir -p $(dir $@)
	$(LOG_CC)$(CC) $(DEPFLAGS) $(USERLAND_CFLAGS) -c $< -o $@

# App images share one link shape. The per-app APP_LIBS_* variables add
# optional libkarm objects through secondary expansion.
.SECONDEXPANSION:
$(BUILD_DIR)/$(APPS_DIR)/%.elf: $(BUILD_DIR)/$(APPS_DIR)/%.o \
    $(BUILD_DIR)/$(APPS_DIR)/%_header.o \
    $(LIBKARM_SYSCALL_OBJ) \
    $(LIBKARM_CRT0_OBJ) \
    $$(APP_LIBS_$$*) $(BUILD_DIR)/$(APPS_DIR)/%_end.o \
    $(APPS_DIR)/image.ld
	$(LOG_LD)$(LD) -T $(APPS_DIR)/image.ld -nostdlib \
	    $(BUILD_DIR)/$(APPS_DIR)/$*.o \
	    $(BUILD_DIR)/$(APPS_DIR)/$*_header.o \
	    $(LIBKARM_SYSCALL_OBJ) \
	    $(LIBKARM_CRT0_OBJ) \
	    $(APP_LIBS_$*) $(BUILD_DIR)/$(APPS_DIR)/$*_end.o \
	    -o $@

$(BUILD_DIR)/$(APPS_DIR)/%.bin: $(BUILD_DIR)/$(APPS_DIR)/%.elf
	$(LOG_OBJCOPY)$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/$(APPS_DIR)/%_blob.o: $(BUILD_DIR)/$(APPS_DIR)/%.bin
	$(LOG_OBJCOPY)$(OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 \
	    --rename-section .data=.app_$*_blob,alloc,load,readonly,data,contents \
	    $< $@

$(KERNEL_ELF): $(OBJS) linker.ld
	$(LOG_LD)$(LD) $(LDFLAGS) $(OBJS) -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(LOG_OBJCOPY)$(OBJCOPY) -O binary $< $@

$(MKFAT32_IMAGE): tools/mkfat32_image.c | $(BUILD_DIR)
	$(Q)mkdir -p $(dir $@)
	$(LOG_HOSTCC)$(HOST_CC) -Wall -Wextra -O2 $< -o $@

$(VIRTIO_BLK_IMG): $(MKFAT32_IMAGE) $(BUILD_DIR)/$(APPS_DIR)/shell.bin | $(BUILD_DIR)
	$(LOG_MKFAT32)$(MKFAT32_IMAGE) $@ $(BUILD_DIR)/$(APPS_DIR)/shell.bin

entry-check: $(KERNEL_ELF)
	$(LOG_CHECK)$(READELF) -h $(KERNEL_ELF) | grep "Entry point address:" | grep -q "$(LOAD_ADDR)"
	$(Q)$(READELF) -s $(KERNEL_ELF) | grep " _start$$" | grep -q "0*$(LOAD_ADDR_HEX)"

qemu: qemu-check entry-check $(KERNEL_BIN)
	$(LOG_QEMU)qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M -nographic \
	    -kernel $(KERNEL_BIN)

qemu-blk: qemu-check entry-check $(KERNEL_BIN) $(VIRTIO_BLK_IMG)
	$(LOG_QEMU)qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M -nographic \
	    -global virtio-mmio.force-legacy=false \
	    -kernel $(KERNEL_BIN) \
	    -drive file=$(VIRTIO_BLK_IMG),if=none,format=raw,id=hd0 \
	    -device virtio-blk-device,drive=hd0

qemu-fs-test: qemu-check entry-check $(KERNEL_BIN) $(VIRTIO_BLK_IMG)
	$(Q)mkdir -p $(dir $(QEMU_FS_TEST_LOG))
	$(LOG_QEMU)status=0; \
	    timeout $(QEMU_FS_TEST_TIMEOUT) qemu-system-aarch64 \
	        -machine virt -cpu cortex-a72 -m 128M -nographic \
	        -global virtio-mmio.force-legacy=false \
	        -kernel $(KERNEL_BIN) \
	        -drive file=$(VIRTIO_BLK_IMG),if=none,format=raw,id=hd0 \
	        -device virtio-blk-device,drive=hd0 \
	        >$(QEMU_FS_TEST_LOG) 2>&1 || status=$$?; \
	    if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then \
	        cat $(QEMU_FS_TEST_LOG); \
	        exit $$status; \
	    fi; \
	    grep -q "storage: initialized" $(QEMU_FS_TEST_LOG) || \
	        { cat $(QEMU_FS_TEST_LOG); exit 1; }; \
	    grep -q "FAT32: mounted" $(QEMU_FS_TEST_LOG) || \
	        { cat $(QEMU_FS_TEST_LOG); exit 1; }; \
	    grep -q "FAT32 root: mounted" $(QEMU_FS_TEST_LOG) || \
	        { cat $(QEMU_FS_TEST_LOG); exit 1; }; \
	    grep -q "FAT32 shell bytes:" $(QEMU_FS_TEST_LOG) || \
	        { cat $(QEMU_FS_TEST_LOG); exit 1; }; \
	    grep -q "FAT32 edit file: mounted" $(QEMU_FS_TEST_LOG) || \
	        { cat $(QEMU_FS_TEST_LOG); exit 1; }; \
	    grep -q "storage app image: FAT32" $(QEMU_FS_TEST_LOG) || \
	        { cat $(QEMU_FS_TEST_LOG); exit 1; }; \
	    printf "qemu-fs-test: log %s\n" "$(QEMU_FS_TEST_LOG)"

qemu-fb: qemu-check entry-check $(KERNEL_BIN)
	$(LOG_QEMU)qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M \
	    -display none -serial stdio \
	    -global virtio-mmio.force-legacy=false \
	    -kernel $(KERNEL_BIN) \
	    -device virtio-gpu-device,xres=640,yres=480

qemu-fb-visible: qemu-check entry-check $(KERNEL_BIN)
	$(LOG_QEMU)qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M \
	    -display gtk,gl=off -serial stdio \
	    -global virtio-mmio.force-legacy=false \
	    -kernel $(KERNEL_BIN) \
	    -device virtio-gpu-device,xres=640,yres=480 \
	    -device virtio-mouse-device

qemu-debug: qemu-check entry-check $(KERNEL_BIN)
	$(LOG_QEMU)qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M -nographic \
	    -kernel $(KERNEL_BIN) -S -s

qemu-net: qemu-check entry-check $(KERNEL_BIN)
	$(LOG_QEMU)qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M -nographic \
	    -netdev user,id=net0 -device virtio-net-device,netdev=net0 \
	    -global virtio-mmio.force-legacy=false \
	    -kernel $(KERNEL_BIN)

qemu-usb: qemu-check entry-check $(KERNEL_BIN)
	$(LOG_QEMU)qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 128M -nographic \
	    -global virtio-mmio.force-legacy=false \
	    -kernel $(KERNEL_BIN) \
	    -device qemu-xhci,id=xhci \
	    -device usb-kbd,bus=xhci.0 \
	    -device usb-mouse,bus=xhci.0

size: $(KERNEL_ELF) $(KERNEL_BIN)
	$(LOG_SIZE)$(SIZE) $(KERNEL_ELF)
	@bytes=$$(wc -c < $(KERNEL_BIN)); \
	printf "kernel.bin: %s bytes (limit: $(KERNEL_SIZE_LIMIT))\n" "$$bytes"; \
	test "$$bytes" -lt $(KERNEL_SIZE_LIMIT)

clean:
	$(Q)rm -rf $(BUILD_DIR)

-include $(DEPS)
