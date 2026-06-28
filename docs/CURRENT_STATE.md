# Current State

This is the short live snapshot of the QEMU desktop and its kernel support.
Historical cleanup details live in `docs/TECH_DEBT_REVIEW.md`.

## Baseline

- Current version target: **v0.9 QEMU desktop baseline**.
- Next version target: **v1.0 stable/debugged QEMU kernel + desktop release**.
- Last verified kernel size: `kernel.bin: 89256 bytes (limit: 100000)`.
- Standard checks for kernel, driver, boot, and ABI changes are `make`,
  `make size`, and `make -C tests test`.
- Targeted runtime checks include `make qemu-fs-test`,
  `timeout 25s make qemu-fb`, `timeout 25s make qemu-usb`, and
  `make qemu-net` when networking changes.

## Boot And Processes

- The default board is QEMU `virt`.
- The kernel runs in EL1 with an identity-mapped kernel/MMIO bootstrap.
- EL0 apps run from per-process `TTBR0_EL1` page tables.
- Each process owns tracked user regions for image, stack, and mmap memory.
  `PROCESS_MAX_USER_REGIONS` is 8.
- App image and stack pages are allocated per spawn from PMM and released by
  process cleanup.
- The fixed user image/stack virtual layout is centralized in
  `kernel/layout.h`.

## Userland

- Shipping apps are C programs under `programs/apps/`: `panel`, `shell`,
  `editor`, `monitor`, and `clock`.
- Apps link against `programs/libkarm` and `programs/libkarmdesk`.
- Userland syscall output helpers are centralized through the small
  `kli_write_cstr()` instead of per-app `write_cstr` copies.
- App images use the KLI1 flat format and are embedded in bootfs.
- When `make qemu-blk` or `make qemu-fs-test` provides the generated FAT32
  virtio-blk disk, the kernel can select FAT32-backed app images through VFS.
- `make stack-check` measures per-function userland C stack usage.

## GUI

- The old `kernel/gui.c` monolith is gone.
- GUI code is split across `kernel/gui_events`, `gui_cursor`, `gui_input`,
  `gui_backing`, `gui_pool`, and `gui_compositor`.
- `kernel/gui.h` is the public aggregate header and no longer includes the
  framebuffer driver header directly.
- The compositor is still kernel-owned. There is no separate desktop server.
- Windows have per-process ownership, title bars, close/minimize/restore,
  focus, z-order, drag, per-window backing buffers, and damage rectangles.
- Cursor shape is arrow/hand, with per-window cursor-region registration for
  owner-drawn controls.

## Input And Drivers

- UART, virtio-input, and USB HID feed the common input queue.
- Mouse buttons are normalized as button indices: `0` left, `1` right,
  `2` middle.
- QEMU `virt` supports virtio-gpu, virtio-blk, virtio-net/DHCP, and xHCI USB
  HID on the development path.
- The network stack is hand-written under `kernel/net/`; DHCP option parsing
  has focused host tests.
- The first v1.0 networking pass reduced virtio-net's static buffers to a
  16-descriptor RX queue plus one shared TX frame, and moved DHCP frame
  scratch space out of the polling thread stack.
- FAT32 parser/VFS behavior has host tests, and the storage integration path is
  covered by `make qemu-fs-test`.

## Syscalls And ABI

- Syscall numbers are pinned in `kernel/syscall_numbers.h` with
  `_Static_assert`s.
- `SYSCALLS.md` is the syscall reference.
- User-pointer validation funnels through `kernel/syscall_helpers.{c,h}`.
- Owner-only window syscall lookup funnels through shared syscall helpers.
- `tests/test_syscall_abi.c` and `tests/test_window_abi.c` pin the ABI details
  that apps depend on.

## Next Engineering Focus

- Next: continue the v1.1 app polish pass across stack usage and userland
  syscall callsites, then rerun the v1.0 QEMU stability sweep.
- Later: GUI size work if `kernel.bin` pressure returns, and xHCI cleanup only
  with USB runtime checks.

## Known Product Gaps

These are not technical-debt blockers; they are future product work:

- no TCP/HTTP client;
- no USB hub support;
- no automated framebuffer screenshot diff test;
- no real Raspberry Pi hardware boot yet;
- no audio or multimedia runtime yet;
- no SMP.
