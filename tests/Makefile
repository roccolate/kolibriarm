CC=gcc
CFLAGS=-I.. -I../drivers -Iunity -g -O0 -Wall -Wextra
SRCS=unity/unity.c main.c test_pmm.c test_kheap.c test_vmm.c test_process.c \
     test_boot_program.c test_bootfs.c test_user_image.c test_user_vm.c \
     test_vfs.c test_tmpfs.c test_fb.c test_dtb.c test_virtio_blk.c \
     ../kernel/mm/pmm.c ../kernel/mm/kheap.c ../kernel/mm/vmm.c \
     ../kernel/boot_program.c ../kernel/bootfs.c \
     ../kernel/process.c ../kernel/tmpfs.c ../kernel/user_image.c ../kernel/user_vm.c ../kernel/vfs.c \
     ../kernel/dtb.c ../kernel/dtb_framebuffer.c \
     ../drivers/fb/fb.c ../drivers/storage/virtio_blk.c
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
