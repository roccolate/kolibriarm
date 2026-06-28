# Roadmap

Honest trajectory for KolibriARM. The first desktop milestone (Phase 10.0–10.5)
is functionally complete on QEMU `virt`: the kernel boots into a graphical
desktop, the panel owns the taskbar, and shell / editor / monitor / clock are
real per-process EL0 apps that own windows, take input, and redraw through the
per-rect compositor path. The host test suite covers the window ABI, IPC,
process isolation, partial redraw, USB HID parsing, FAT32 integration, DHCP
options, and the syscall number table; `make`, `make size`, and
`make -C tests test` are the baseline checks. Latest verified size:
`kernel.bin: 89256 bytes (limit: 100000)`. This is the current v0.9
baseline.

This file is intentionally short. It separates current verification from
future work so "done" history does not hide real blockers. The "version
targets" table at the bottom still records where the milestones landed.

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

The kernel debug console (`k>`) ships with `help`, `mem`, `ps`, `ticks`,
`status`, `mouse`, `click`, and `key` for headless diagnostics.

---

## Current verification checklist

The v0.9 QEMU desktop baseline is complete. It does not require Raspberry Pi
hardware, hubs, SMP, TCP/HTTP, or multimedia. The v1.0 target is a stable,
debugged, repeatable QEMU kernel and desktop release. Any release tag or
kernel, driver, syscall, boot, or userland ABI change still requires repeatable
build/test commands, a stable syscall table, honest docs, and a verified
desktop smoke run.

### 1. Build and host tests

Required before release tags and before landing risky runtime changes:

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
- `make qemu-fs-test` must confirm the generated FAT32 virtio-blk image mounts
  and the FAT32-backed user-image path is selected.
- `make qemu-fb-visible` must be checked manually: cursor movement, click to
  raise, panel launch buttons, title-bar close, minimize/restore through the
  taskbar, editor typing, and opening all four apps without a crash.

There is no scripted screenshot test yet. That is useful future work, not a
current baseline blocker.

### 3. ABI and docs

Keep these in sync:

- `SYSCALLS.md` documents `svc #0`, syscall number in `x8`, arguments in
  `x0..x6`, and return value in `x0`.
- `kernel/syscall_numbers.h` keeps `_Static_assert` pins for every syscall.
- `programs/libkarm/syscall.S` exposes `__syscall0` through `__syscall7`.
- `programs/libkarmdesk/gui.h` wraps every implemented window/compositor call.
- `README.md` describes QEMU desktop support as the current state and RPi as
  planned hardware work.

New syscalls should be avoided unless they close a real runtime gap. If one is
unavoidable, it needs the number, dispatch case, SYSCALLS.md row, wrapper, and
host test in the same change.

### 4. Known non-blockers

These are explicitly future product work, not current baseline blockers:

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

## v1.0 work plan

The v1.0 target is a stable, debugged QEMU kernel and desktop release. Work in
this order:

- Continue hardening `kernel/net/` and `drivers/net/virtio_net.c`. The first
  buffer-footprint pass reduced virtio-net to 16 RX descriptors and one shared
  TX frame buffer; verify further work with `make qemu-net`.
- Run a QEMU-focused debug/stability sweep across boot, storage, display,
  input, networking, syscalls, and process cleanup.
- Touch `kernel/gui_*` or `drivers/usb/xhci.c` only if v1.0 checks expose
  regressions or size pressure.
- Keep `programs/apps/` stack usage and userland syscall-callsite review for
  v1.1 unless an app bug blocks QEMU stability.

## Later work

The next set of candidates after v1.0, in rough order of return on effort:

- v1.1 userland/app polish: stack usage, syscall-callsite review, and app UX
  bugs that do not block v1.0. The first pass has centralized app debug-string
  writes through the small `kli_write_cstr` libkarm helper; keep
  `make stack-check` in the loop.
- RPi 4 PCIe host bridge setup so the VL805 xHCI controller appears to the
  existing USB stack.
- USB hub support after root-port HID remains stable.
- Scripted screenshot or framebuffer diff tests for desktop regressions.
- Cleaner shutdown syscall and panel policy branch.
- TCP/HTTP for `wget`-style apps.
- Larger EL0 stacks if bigger apps need them.
- SMP after the uniprocessor desktop is boringly stable.
- Engine and multimedia runtime.

---

## Style boundaries (carried over from AGENTS.md)

- No libc, no POSIX, no Linux compatibility layer.
- AArch64 asm only at the CPU boundary, with a comment when control flow
  is subtle.
- Reuse existing modules before adding new ones: `kernel/mm`,
  `kernel/sched`, `kernel/timer`, `drivers/irq`, `drivers/uart`,
  `kernel/gui_*`.
- Prefer a `drivers/boards/qemu_virt/` platform layer before touching
  RPi 4.
- Port ideas from KolibriOS, not its x86 asm. Keep ABI choices explicit and
  pinned by tests before apps depend on them.
- Keep the kernel readable in one sitting. If a module stops fitting on
  a few pages, split it before adding features.

---

## Version targets

| Version | Status | Milestone | Phases |
| --- | --- | --- | --- |
| v0.1 | done | Boots, UART output | 0 |
| v0.2 | done | Memory management working | 0-1 |
| v0.3 | done | Preemptive multitasking | 0-2 |
| v0.4 | done | Real process address spaces | 0-2.5 |
| v0.5 | done | Drivers + framebuffer | 0-3 |
| v0.6 | done | Board abstraction cleanup | 0-3.6 |
| v0.7 | done | Multiple real EL0 apps + per-process windows | 0-10.0-10.4 |
| v0.8 | done | QEMU desktop: panel + taskbar + 4 apps + mouse | 0-10.5 |
| v0.9 | current | QEMU desktop baseline: tech-debt review closed, kernel under size limit, checks documented | 0-10.5 + cleanup |
| v1.0 | next | Stable, debugged QEMU kernel + desktop release | v0.9 + QEMU stability |
| v1.1 | in progress | Userland/apps polish: stack usage and syscall-callsite review | v1.0 + apps |
| v1.5 | planned | Running on real RPi 4 hardware | v1.0 + RPi bring-up |
| v2.0 | future | Engine and multimedia runtime | 9-15 |
