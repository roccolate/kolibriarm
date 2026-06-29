# Current State

This is the short live snapshot of the QEMU desktop and its kernel support.
Historical cleanup details live in `TECH_DEBT_REVIEW.md`.

## Baseline

- Current version target: **v0.9 QEMU desktop baseline**.
- Next version target: **v1.0 stable/debugged QEMU kernel + desktop release**.
- Last verified kernel size: `kernel.bin: 92696 bytes (limit: 100000)`.
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
  `editor`, `files`, `monitor`, and `clock`.
- Apps link against `programs/libkarm` and `programs/libkarmdesk`.
- Userland syscall output helpers are centralized through the small
  `kli_write_cstr()` instead of per-app `write_cstr` copies.
- App images use the KLI1 flat format and are embedded in bootfs.
- The panel launcher row is `shell`, `editor`, `files`, `monitor`, `clock`;
  nested panel launches are intentionally not exposed, and clicking an already
  running app restores/focuses it instead of spawning a duplicate.
- The shell supports simple fixed-buffer commands, including `pwd`, `cd`,
  cwd-relative `ls`, and `cat`.
- The editor shows its active path and open/save status, and can create FAT32
  root files when editing `/fat/<name>`.
- The `files` app lists `/fat`, opens the selected file in editor, creates
  8.3 root files, renames files, deletes files after confirmation, and exits
  through Ctrl-Q or the close button.
- When `make qemu-blk` or `make qemu-fs-test` provides the generated FAT32
  virtio-blk disk, the kernel can select FAT32-backed app images through VFS.
- `make stack-check` measures per-function userland C stack usage; the current
  maximum is 368 bytes after moving large app state into anonymous user
  mappings.

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
- `SYS_OPEN` accepts access mode bits plus `O_CREAT = 0x40`; creation is
  currently supported only for FAT32 root files under `/fat/<8.3-name>`.
- Dynamic `/fat/<name>` opens mount FAT32 root files on demand instead of
  requiring every file to be pre-mounted during boot.

## Syscalls And ABI

- Syscall numbers are pinned in `kernel/syscall_numbers.h` with
  `_Static_assert`s.
- `SYSCALLS.md` is the syscall reference.
- User-pointer validation funnels through `kernel/syscall_helpers.{c,h}`.
- Owner-only window syscall lookup funnels through shared syscall helpers.
- `tests/test_syscall_abi.c` and `tests/test_window_abi.c` pin the ABI details
  that apps depend on.

## Security Baseline

ArmoniOS currently targets a single-user, hardened QEMU desktop baseline. It
does provide:

- EL0 apps isolated from the EL1 kernel;
- per-process user page tables and tracked user regions;
- syscall user-pointer validation through shared helpers;
- owner checks for window operations that mutate another process's window;
- cleanup of process-owned PMM pages and GUI resources on process release.

It does not yet claim:

- multi-user accounts;
- file permissions;
- capabilities;
- signed or trusted app policy;
- a complete sandbox model for malicious user programs.

## Next Engineering Focus

- Next: run the desktop-core gates after the `files` / `O_CREAT` pass, then
  decide whether this behavior is part of v1.0 or a later label.
- Later: minimal userland-only engine helpers after the desktop-core flow is
  stable; GUI size work if `kernel.bin` pressure returns; xHCI cleanup only
  with USB runtime checks.

## Known Product Gaps

These are not technical-debt blockers; they are future product work:

- no TCP/HTTP client;
- no USB hub support;
- no automated framebuffer screenshot diff test;
- no real Raspberry Pi hardware boot yet;
- no audio or multimedia runtime yet;
- no SMP.
