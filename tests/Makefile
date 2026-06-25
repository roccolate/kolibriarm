CC=gcc
OBJCOPY=objcopy
CFLAGS=-I.. -I../drivers -Iunity -g -O0 -Wall -Wextra -DKOLIBRIARM_TEST
BUILD_DIR := ../build
APPS_DIR  := programs/apps

# Shipping app .bin files live in the main build tree. They are
# pure-byte images with a known layout (KLI1 header + code + rodata).
# Convert each .bin into a host-architecture .o with a small section
# of raw bytes; the test then reads the .bin back through its
# embedded symbol to assert image_size covers the whole blob. The
# production kernel does the same with aarch64 objcopy, so this
# test catches the same shape of bug in the BUILD process.
APP_BLOB_BINS  := $(BUILD_DIR)/$(APPS_DIR)/shell.bin \
                  $(BUILD_DIR)/$(APPS_DIR)/editor.bin \
                  $(BUILD_DIR)/$(APPS_DIR)/monitor.bin \
                  $(BUILD_DIR)/$(APPS_DIR)/clock.bin \
                  $(BUILD_DIR)/$(APPS_DIR)/panel.bin
APP_BLOB_OBJS  := $(patsubst %.bin,%.host_blob.o,$(APP_BLOB_BINS))

SRCS=unity/unity.c main.c \
     test_pmm.c test_kheap.c test_vmm.c test_process.c \
     test_boot_program.c test_bootfs.c test_user_image.c test_user_vm.c \
     test_user_image_runtime.c \
     test_panel_boot_recovery.c \
     test_vfs.c test_tmpfs.c test_fat32.c test_ipc.c test_font.c \
     test_gui.c test_fb.c test_dtb.c test_virtio_blk.c test_input.c \
     test_hid.c test_pci.c test_hid_driver.c test_usb_core.c \
     test_pci_assign.c test_hid_state.c test_hid_init.c \
     test_syscall_abi.c test_process_isolation.c test_window_abi.c \
     ../kernel/mm/pmm.c ../kernel/mm/kheap.c ../kernel/mm/vmm.c \
     ../kernel/boot_program.c ../kernel/bootfs.c \
     ../kernel/panel_boot_recovery.c \
     ../kernel/fat32.c ../kernel/font.c ../kernel/gui.c \
     ../kernel/ipc.c ../kernel/process.c \
     ../kernel/tmpfs.c \
     ../kernel/user_image.c ../kernel/user_vm.c ../kernel/vfs.c \
     ../kernel/dtb.c ../kernel/dtb_framebuffer.c \
     ../drivers/fb/fb.c ../drivers/storage/virtio_blk.c \
     ../drivers/input/input.c \
     ../drivers/usb/hid.c ../drivers/usb/hid_driver.c \
     ../drivers/usb/uhci.c ../drivers/usb/usb_core.c \
     ../drivers/pci/pci.c test_pl011_stub.c test_panel_boot_recovery_stub.c \
     $(APP_BLOB_OBJS)

TARGET=run_tests

all: $(TARGET)

$(TARGET): $(SRCS) $(APP_BLOB_OBJS)
	$(CC) $(CFLAGS) $(SRCS) -o $@

%.host_blob.o: %.bin
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	    --rename-section .data=.app_host_blob,alloc,load,readonly,data,contents \
	    $< $@

.PHONY: test

test: all
	./$(TARGET)

.PHONY: clean

clean:
	rm -f $(TARGET) *.o
