CC=gcc
CFLAGS=-I.. -I../drivers -Iunity -g -O0 -Wall -Wextra -DKOLIBRIARM_TEST

SRCS=unity/unity.c main.c \
     test_pmm.c test_kheap.c test_vmm.c test_process.c \
     test_boot_program.c test_bootfs.c test_user_image.c test_user_vm.c \
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
     ../drivers/pci/pci.c test_pl011_stub.c test_panel_boot_recovery_stub.c

TARGET=run_tests

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: test

test: all
	./$(TARGET)

.PHONY: clean

clean:
	rm -f $(TARGET) *.o
