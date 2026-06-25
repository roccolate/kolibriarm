# Roadmap

Honest trajectory for KolibriARM. The first desktop milestone (Phase 10.0–10.5)
is functionally complete on QEMU `virt`: the kernel boots into a graphical
desktop, the panel owns the taskbar, and shell / editor / monitor / clock are
real per-process EL0 apps that own windows, take input, and redraw through the
per-rect compositor path. The host test suite covers the window ABI, IPC,
process isolation, partial redraw, USB HID parsing, and the syscall number
table; `make` and `make -C tests test` both pass.

This file is intentionally short. It separates the alpha closure checklist
from post-alpha work so "done" history does not hide real blockers. The
"version targets" table at the bottom still records where the milestones
landed.

A phase is "done" only when its exit criteria pass on real QEMU runs (and,
where applicable, on real hardware).

---

## What is in place today

The system boots on QEMU `virt`, brings up virtio-gpu (640×480), virtio-input,
GICv2, virtio-blk/FAT32, virtio-net/DHCP, xHCI USB HID enumeration, and the
serial `k>` debug console. The kernel lands on a graphical desktop whose first
userland process is the panel taskbar at the bottom of the screen.

Per-app, per-process windowing is real:

- Each app under `programs/apps/` builds as a flat AArch64 image, is
  registered in `kernel/boot_program.c`, exposed through bootfs under
  `/kolibri/<name>`, and is launched as its own process with its own page
  table and stack.
- `SYS_SPAWN_ARGV` (8) places `argc` in `x0` and `&argv[0]` in `x1` per the
  AArch64 procedure-call ABI. The shell splits `run X Y Z` on whitespace and
  forwards the tokens; the editor prints the received argv.
- Per-window BGRA backing buffer in the kernel. App draws land in the
  backing; the compositor blits during `gui_draw`. Drags and focus changes
  carry content with the window.
- Coalesced damage-rectangle tracking (cap 32 + "full" sentinel). Every
  app pushes the smallest content-local rect that covers its last batch
  of draws through `SYS_WINDOW_FLUSH` (80). The strict tests in
  `tests/test_gui.c` lock down the in-rect fill, off-screen no-op, and
  multiple disjoint rects.
- Kernel-owned GUI compositor with per-process windows, focus, click-to-
  raise, window dragging, title bars with text, title-bar close events,
  16×16 cursor with arrow/hand shapes, vertical gradient background.

The ABI is frozen. `kernel/syscall_numbers.h` lists every implemented syscall
with a `_Static_assert` next to each value; `tests/test_syscall_abi.c`
exercises each dispatch entry at runtime. Any new syscall must add a row in
SYSCALLS.md, an entry in `syscall_numbers.h`, the dispatch case in
`kernel/syscall.c`, and a host test — in the same commit.

The kernel debug console (`k>`) still ships with `help`, `mem`, `ps`,
`ticks`, `storage`, `fb`, `mouse`, and `click` for headless diagnostics.

---

## Alpha closure checklist

Alpha is a QEMU desktop release. It does not require Raspberry Pi hardware,
hubs, SMP, TCP/HTTP, or multimedia. It does require repeatable build/test
commands, a stable syscall table, honest docs, and a manually verified desktop
session.

### 1. Build and host tests

Required before tagging alpha:

- `make`
- `make size`
- `make -C tests test`
- `make -B apps` after userland or Makefile changes

The size gate is intentionally tight: `kernel.bin` must stay under
`KERNEL_SIZE_LIMIT` in `Makefile`.

### 2. QEMU smoke checks

Run these after kernel, boot, driver, syscall, or userland changes:

- `timeout 8s make qemu-fb` must reach `panel: ready`.
- `timeout 8s make qemu-usb` must enumerate both directly attached HID
  devices and print `USB HID: 2 devices`.
- `make qemu-fb-visible` must be checked manually: cursor movement, click to
  raise, panel launch buttons, title-bar close, minimize/restore through the
  taskbar, editor typing, and opening all four apps without a crash.

There is no scripted screenshot test yet. That is useful post-alpha work, not
an alpha blocker.

### 3. ABI and docs freeze

Before alpha, keep these in sync:

- `SYSCALLS.md` documents `svc #0`, syscall number in `x8`, arguments in
  `x0..x6`, and return value in `x0`.
- `kernel/syscall_numbers.h` keeps `_Static_assert` pins for every syscall.
- `programs/libkarm/syscall.S` exposes `__syscall0` through `__syscall7`.
- `programs/libkarmdesk/gui.h` wraps every implemented window/compositor call.
- `README.md` describes QEMU desktop support as the current state and RPi as
  planned hardware work.

New syscalls before alpha should be avoided unless they close a release
blocker. If one is unavoidable, it needs the number, dispatch case,
SYSCALLS.md row, wrapper, and host test in the same change.

### 4. Known non-blockers

These are explicitly allowed to remain after alpha:

- `sys_window_show` / `sys_window_hide`; minimize/restore already covers the
  visible desktop workflow.
- Automated screenshot diffing.
- USB hub support.
- Raspberry Pi 4 PCIe/VL805 bring-up.
- TCP/HTTP clients.
- SMP and secondary-core startup.
- Engine and multimedia runtime.

---

## Completed desktop work

The following work is complete and should stay out of the blocker list unless
regressed:

- Flat C userland apps under `programs/apps/`, linked with `libkarm`,
  `libkarmdesk`, and shared `crt0.S`.
- Shell / editor / monitor / clock launched as separate EL0 processes through
  bootfs and the panel taskbar.
- Per-window backing buffers, damage rectangles, title bars, close events,
  minimize/restore, resize events, window bounds syscalls, and cursor regions.
- Shell scrollback, prompt placement, editor caret movement, arrow keys, Page
  Up / Page Down input parsing.
- xHCI USB HID enumeration for directly attached QEMU keyboard and mouse.
- Panel restart policy through `panel_boot_run_with_recovery`.

---

## After alpha

The next set of candidates beyond alpha, in rough order of return on effort:

- RPi 4 PCIe host bridge setup so the VL805 xHCI controller appears to the
  existing USB stack.
- USB hub support after root-port HID remains stable.
- Scripted screenshot or framebuffer diff tests for desktop regressions.
- Cleaner shutdown syscall and panel policy branch.
- TCP/HTTP for `wget`-style apps.
- Larger EL0 stacks or stack-usage tooling for bigger apps.
- SMP after the uniprocessor desktop is boringly stable.
- Engine and multimedia runtime.

---

## Style boundaries (carried over from AGENTS.md)

- No libc, no POSIX, no Linux compatibility layer.
- AArch64 asm only at the CPU boundary, with a comment when control flow
  is subtle.
- Reuse existing modules before adding new ones: `kernel/mm`,
  `kernel/sched`, `kernel/timer`, `drivers/irq`, `drivers/uart`,
  `kernel/gui`.
- Prefer a `drivers/boards/qemu_virt/` platform layer before touching
  RPi 4.
- Port ideas from KolibriOS, not its x86 asm. The 8×8 font and the `KOS`
  header are the first ports; syscall IDs, IPC semantics, and window list
  layout are next.
- Keep the kernel readable in one sitting. If a module stops fitting on
  a few pages, split it before adding features.

---

## Version targets

| Version | Milestone                                       | Phases                |
|---------|-------------------------------------------------|-----------------------|
| v0.1    | Boots, UART output                              | 0                     |
| v0.2    | Memory management working                       | 0–1                   |
| v0.3    | Preemptive multitasking                         | 0–2                   |
| v0.4    | Real process address spaces                     | 0–2.5                 |
| v0.5    | Drivers + framebuffer                           | 0–3                   |
| v0.6    | Board abstraction cleanup                       | 0–3.6                 |
| v0.7    | Multiple real EL0 apps + per-process windows    | 0–10.0–10.4           |
| v0.8    | QEMU desktop: panel + taskbar + 4 apps + mouse  | 0–10.5                |
| v1.0    | Usable on QEMU: real desktop, real apps         | 0–10.5                |
| v1.5    | Running on real RPi 4 hardware                  | 0–10.5 + RPi bring-up |
| v2.0    | Engine and multimedia runtime                   | 9–15                  |
