# Kernel Technical-Debt Review

Scope: `kernel/`, `drivers/`, panel/apps support code, and the host tests that
pin those contracts.

This review is closed. It was the execution guide for paying down the kernel
debt found during the panel/desktop work and the follow-up C-code audit. Keep
this file as the closure summary; do not add new backlog here. New work belongs
in `ROADMAP.md`, focused issues, or a fresh review document.

## Verification Baseline

Use this baseline before landing kernel, driver, userland ABI, or boot-path
changes:

```sh
make
make size
make -C tests test
```

Additional targeted checks:

```sh
make stack-check
make qemu-fs-test
timeout 25s make qemu-fb
timeout 25s make qemu-usb
```

Current closure size: `kernel.bin: 89040 bytes (limit: 100000)`.

## Closed Items

| Item | Status | Result |
| --- | --- | --- |
| 0 | closed | EL0 launch path and argv stack packing have host tests. |
| 1 | closed | The old `kernel/gui.c` monolith was split into focused `kernel/gui_*` modules. |
| 2 | closed | Owner-window syscall checks are centralized in `syscall_helpers`. |
| 3 | closed | Unchecked GUI title/flag helpers use `_internal` names. |
| 4 | closed | User-buffer and user-cstring validation funnels through syscall helpers. |
| 5 | closed | KLI1 image-format structs/constants moved to `user_image_format.h`. |
| 6 | closed | Process user-region slots were raised to 8 and EL0 stack guard spacing is asserted. |
| 7 | closed | Boot init status is tracked and visible from `k> status`. |
| 8 | closed | Numeric formatting lives in `print.c`; PL011 is raw character transport. |
| 9 | closed | User layout, SPSR values, and user-fault exit constants are centralized. |
| 10 | closed | `make qemu-fs-test` covers FAT32 storage/VFS boot integration. |
| 11 | closed | `kernel/gui.h` no longer hides the framebuffer driver dependency. |
| 12 | closed | The `k>` console dispatch/help path uses a command table. |
| 13 | closed | Per-process image/stack slots are PMM-owned pages, not static BSS arrays. |
| 14 | closed | Panel recovery runs through generic callbacks instead of raw board params. |
| 15 | closed | Userland stack usage is measured by `make stack-check`. |
| 16 | closed | Scheduler next-runnable dispatch is centralized in `process_dispatch_next`. |
| 17 | closed | GUI backing-buffer resize is atomic and tested. |
| 18 | closed | Compiler attributes funnel through `kernel/kernel_compiler.h`. |
| 19 | closed | DHCP option parsing has a pure helper and host tests. |
| 20 | closed | `make help` exists and default build output is compact. |

## Current Architecture Notes

- GUI event queues, cursor/drag handling, input dispatch, backing buffers,
  window lifecycle, and compositor code live in focused `kernel/gui_*` files.
- Syscall ownership checks and user-pointer validation live in
  `kernel/syscall_helpers.{c,h}`.
- Fixed user image/stack virtual layout lives in `kernel/layout.h`.
- AArch64 saved-program-state constants live in `kernel/aarch64_state.h`.
- User-fault exit sentinel values live in `kernel/user_exit.h`.
- KLI1 image-format constants live in `kernel/user_image_format.h`.
- Boot status lives in `kernel/init_status.{c,h}`.
- DHCP option parsing lives in `kernel/net/dhcp_options.{c,h}`.

## Current Follow-Up Pointer

Do not reopen this closed backlog for new optimization work. The latest
file-by-file kernel and driver pass left the tree clean at
`9e66d71 review(core): harden kernel and driver paths`.

The next best technical target belongs in `ROADMAP.md`: compact
`kernel/net/` and `drivers/net/virtio_net.c`, mainly the static RX/TX queue
buffers. Verify that work with the baseline checks plus `make qemu-net`.
Leave `programs/apps/` stack usage and userland syscall-callsite review for
the v1.1 userland pass.

## Follow-Up Policy

The original backlog is complete. Future debt should be tracked as a new
focused document or issue with:

- the affected files/subsystems;
- the smell or risk;
- the intended direction;
- exact verification commands;
- QEMU/manual checks when host tests cannot cover runtime behavior.
