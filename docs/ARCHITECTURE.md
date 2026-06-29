# Architecture

ArmoniOS is a compact monolithic AArch64 kernel for the QEMU `virt` machine,
with Raspberry Pi work kept behind board layers. This document describes the
current architecture; detailed ABI tables live in `SYSCALLS.md`, and fixed
addresses live in `MEMORY_MAP.md`.

## Privilege Model

- EL1: kernel, drivers, scheduler, VFS, GUI compositor, and syscall handling.
- EL0: freestanding user apps.
- EL2/EL3: not used by the current QEMU path.

There is no driver isolation or POSIX compatibility layer. Drivers are linked
into the kernel and are expected to stay small and auditable.

## Boot

QEMU loads the kernel at the board link address and passes a DTB. The early path
is:

```text
boot/start.S
  -> clear BSS
  -> set early stack
  -> call kernel_main

kernel_main
  -> board early init / UART
  -> DTB memory discovery
  -> PMM, heap, VMM/MMU
  -> VFS/bootfs/tmpfs
  -> IRQ/timer/scheduler setup
  -> optional storage/FAT32, display, network, input, USB
  -> panel boot with recovery
  -> scheduler start
```

Boot phase status is tracked in `kernel/init_status.{c,h}` and visible through
`k> status`.

## Memory

The current QEMU implementation keeps the kernel and MMIO identity-mapped while
EL0 processes run with per-process `TTBR0_EL1` page tables. Fixed user image and
stack layout is centralized in `kernel/layout.h`.

Each process tracks user-accessible regions in `process->user_regions[]`.
Syscalls validate user pointers through `kernel/syscall_helpers.{c,h}` before
copying data across the EL0/EL1 boundary.

PMM-owned regions are released by process cleanup. The loader uses PMM pages for
per-process image and stack storage; these are no longer static BSS arrays.

## Processes And Scheduling

`kernel/process.{c,h}` owns process slots, saved EL0 context, user-region
metadata, page-table ownership, and zombie cleanup. Dispatch to the next
runnable process funnels through:

```c
int process_dispatch_next(process_t *current, exception_frame_t *frame,
                          process_dispatch_policy_t policy);
```

The dispatch policy distinguishes exit/fault paths from yield/preemption paths.
Timer IRQ and syscall yield share the same activation logic.

## Syscalls

Syscalls use `svc #0`:

```text
x8      syscall number
x0..x6  arguments
x0      return value; negative values are kernel error codes
```

Numbers are pinned in `kernel/syscall_numbers.h` and covered by
`tests/test_syscall_abi.c`. `SYSCALLS.md` is the reference table.

## Drivers And Board Boundary

Generic kernel code includes `drivers/board.h`. Board-specific physical
addresses and platform details live under `drivers/boards/<board>/`.

Driver examples:

- `drivers/uart/pl011.c` for UART character transport;
- `drivers/fb/fb.c` for linear framebuffer primitives;
- `drivers/gpu/virtio_gpu.c` for QEMU scanout;
- `drivers/storage/virtio_blk.c` and `drivers/storage/emmc.c`;
- `drivers/net/virtio_net.c`;
- `drivers/usb/xhci.c` plus HID helpers.

`kernel/gui.h` forward-declares `fb_t`; implementation files include
`fb/fb.h` only where concrete framebuffer fields are needed.

## GUI

The GUI is a kernel-owned compositor, not a userland display server. The old
`kernel/gui.c` monolith is split by responsibility:

- `gui_events`: per-window event queues;
- `gui_cursor`: cursor state, drag state, cursor regions;
- `gui_input`: hit testing and input dispatch;
- `gui_backing`: owner-drawn backing buffers;
- `gui_pool`: window lifecycle and lookup;
- `gui_compositor`: damage tracking, drawing, render entry points.

Windows are process-owned. Owner-only operations are checked through syscall
helpers; panel operations that intentionally cross process ownership use
specific syscalls such as `sys_window_focus` and `sys_window_for_pid`.

## Userland

User apps live under `programs/apps/` and link against:

- `programs/libkarm` for syscall trampolines, `crt0`, and small C helpers;
- `programs/libkarmdesk` for GUI wrappers.

Shipping apps are `panel`, `shell`, `editor`, `files`, `monitor`, and
`clock`.

## Filesystems And Storage

The VFS exposes bootfs, tmpfs, and FAT32 mounts. FAT32 root files can be opened
dynamically through `/fat/<name>`, and `SYS_OPEN` supports `O_CREAT` for
`/fat/<8.3-name>`. FAT32 parser/VFS behavior has host tests, and the QEMU
storage integration path is checked by `make qemu-fs-test`.

## Coding Standards

- Freestanding C11 and small AArch64 assembly boundaries.
- No libc/POSIX assumptions in kernel or userland runtime.
- Keep hardware behavior explicit; avoid vague abstraction names.
- Prefer focused host tests for pure C logic.
- Keep kernel binary size under `KERNEL_SIZE_LIMIT`.
