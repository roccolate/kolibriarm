# KolibriARM

> A minimal, fast, assembly-first operating system for AArch64 — inspired by KolibriOS and MenuetOS.

[![License: GPL-2.0](https://img.shields.io/badge/License-GPL%202.0-blue.svg)](LICENSE)
[![Architecture](https://img.shields.io/badge/arch-AArch64-green.svg)]()
[![Language](https://img.shields.io/badge/lang-C%20%2B%20ASM-orange.svg)]()
[![Status](https://img.shields.io/badge/status-QEMU%20desktop-blue.svg)]()

---

## What is KolibriARM?

KolibriARM is a bare-metal operating system for ARM64 (AArch64) processors, written entirely in C and AArch64 assembly. It takes direct inspiration from [KolibriOS](https://kolibrios.org) and [MenuetOS](https://www.menuetos.net/) — two x86 operating systems celebrated for their extreme compactness, speed, and elegance — and brings those principles to modern ARM hardware.

**Core philosophy:**
- Every byte is intentional. No unnecessary abstraction layers.
- The kernel fits in your head. Small enough to read in a weekend.
- No libc. No POSIX. No Linux compatibility layer. Just clean system calls.
- Keep boot fast and observable on QEMU before claiming hardware numbers.
- Keep the memory footprint small enough for constrained ARM boards.

---

## Current Status

> **A functional QEMU desktop for a small AArch64 OS.** The kernel boots on
> QEMU `virt`, brings up memory management, enables an identity-mapped MMU,
> runs EL0 app processes with syscall and timer-IRQ context switches, and keeps
> the minimal `k>` debug console as a fallback. With `make qemu-blk`, QEMU
> boots with a generated FAT32 virtio-blk image and reloads apps through the
> VFS path.
>
> Userland apps are freestanding C programs built as flat AArch64 images under
> `programs/apps/`, linked with `programs/libkarm` and
> `programs/libkarmdesk`, embedded through bootfs, and exposed under
> `/kolibri/<name>`. The QEMU desktop has per-process window ownership,
> a panel taskbar, shell / editor / monitor / clock apps, cursor/focus/drag
> handling, per-window backing buffers, and rect-based redraw. See
> [ROADMAP.md](ROADMAP.md) for what is still missing.

| Component         | Status       | Notes                                  |
|-------------------|-------------|----------------------------------------|
| Bootloader        | Working      | AArch64 ASM, QEMU virt tested          |
| Physical memory   | Working      | Bitmap allocator, host tests           |
| Virtual memory    | Working      | Identity map, per-process user tables, user flags, unmap |
| Scheduler         | Working      | Round-robin, timer IRQ, kernel + EL0 threads |
| IRQ dispatch      | Working      | GICv2, timer PPI, UART RX, C handler table |
| UART driver       | Working      | PL011 TX polling, RX IRQ ring, QEMU console input echo |
| Syscalls          | Working      | frozen implemented ABI for process, memory, VFS, IPC, info, window/compositor |
| Userland          | Working      | Freestanding C EL0 apps with `libkarm`, `libkarmdesk`, and shared `crt0` |
| Framebuffer       | Working      | virtio-gpu scanout, primitives, bitmap text, alpha |
| Storage           | Working      | virtio-blk sector read/write, FAT32 read + limited overwrite |
| Filesystem        | Working      | Fixed VFS, bootfs seed, tmpfs, FAT32 root 8.3 lookup |
| GUI               | Working      | Kernel compositor has owner windows, panel/taskbar, backing buffers, title bars, damage rects, and app events |
| Mouse / cursor    | Working      | virtio-input, USB HID, cursor movement, drag, click-to-raise, and hand regions |
| Networking        | Working      | from-scratch virtio-net + DHCP; next cleanup target is buffer footprint |
| RPi 4 port        | Builds       | Not booted on real hardware yet |

## Current Focus

The project is at the **v0.9 QEMU desktop baseline**. The v1.0 target is a
stable, debugged, repeatable QEMU kernel and desktop release. Current
engineering focus is reducing static kernel/driver footprint and tightening
runtime checks before the v1.1 userland app review. Latest verified size:
`kernel.bin: 89040 bytes (limit: 100000)`. Read [ROADMAP.md](ROADMAP.md) for
the full breakdown.

Baseline already in place:

- [x] Ship flat C userland apps under `programs/apps/`, registered by name in
      the loader and exposed under `/kolibri/<name>`.
- [x] Add window syscalls (`sys_window_create`, `sys_window_draw_text`,
      `sys_window_event`, `sys_window_destroy`) with per-process ownership.
- [x] Consume the queued mouse events: visible cursor, click-to-raise,
      window drag, focus visualization.
- [x] Add a panel process that owns the taskbar and launches apps by
      clicking icons.
- [x] Ship four real apps: `shell`, `editor`, `monitor`, `clock`
      as windowed desktop apps.
- [x] Port KolibriOS's 8x8 font (`8X8ISXP`-shaped, ASCII 32-126).
- [x] Load native `KLI1` flat app images from bootfs; unknown image magic is
      rejected instead of treated as a compatibility format.
- [x] USB HID foundations: PCI ECAM scan + BAR auto-assignment,
      xHCI poll-mode driver with real control and interrupt-in transfers,
      boot-protocol HID report parser, descriptor walker, and a
      kernel-wide poll loop that feeds the existing `input_queue`.
      `make qemu-usb` boots the kernel with `qemu-xhci + usb-kbd +
      usb-mouse` and reaches `USB: controller initialized` /
      `USB: device on port ...` / `USB: enumeration ok` / `USB HID:
      2 devices`.

Next v1.0 cleanup targets:

- Compact `kernel/net/` and `drivers/net/virtio_net.c`, especially the static
  virtio-net RX/TX buffers; verify with `make qemu-net`.
- Leave `programs/apps/` stack usage and userland syscall-callsite review for
  v1.1 unless an app bug blocks QEMU stability.
- Revisit GUI size or xHCI internals only with the relevant QEMU runtime checks
  in the loop.

Still out of scope:
- SMP, full FAT32 write beyond the current create/delete/rename + chain-grow
  support, and real HTTP client.
- USB hub support.
- RPi 4 hardware bring-up (it builds, but PCIe host bridge setup for
  the VL805 xHCI controller is not wired yet).

---

## Target Hardware

**Primary target:** QEMU `virt` machine (AArch64, Cortex-A72)
**Planned hardware target:** Raspberry Pi 4 / 5 (BCM2711 / BCM2712)
**Future targets:** Any Cortex-A board with open peripheral documentation

---

## Building

### Requirements

- WSL2 (Ubuntu 22.04+) or any Linux system
- `gcc-aarch64-linux-gnu` — AArch64 cross-compiler
- `qemu-system-aarch64` — ARM emulator
- `gdb-multiarch` — debugger
- `make`

### Install dependencies (Ubuntu / WSL2)

```bash
sudo apt update && sudo apt install -y \
  qemu-system-arm \
  gcc-aarch64-linux-gnu \
  binutils-aarch64-linux-gnu \
  gdb-multiarch \
  make
```

### Build and run

```bash
# Clone the repository
git clone https://github.com/roccolate/kolibriarm
cd kolibriarm

# Build the default QEMU virt board
make

# List documented build and QEMU targets
make help

# Show full compiler/linker commands instead of compact build lines
make V=1

# Measure per-function C stack usage for userland apps
make stack-check

# Equivalent explicit board selection
make BOARD=qemu_virt

# Run in QEMU
make qemu

# In the shell app, try:
# help
# ls
# ps
# ticks
# mem
# run editor
# run editor myfile.txt    # passes myfile.txt as argv[1] to the editor
# run monitor
# run clock
# kill last
# exit

# Run in QEMU with a generated FAT32 virtio-blk image
make qemu-blk

# Smoke-test the FAT32 storage/VFS boot path under QEMU
make qemu-fs-test

# Run in QEMU with virtio-gpu headless and boot the desktop
make qemu-fb

# Run in QEMU with a visible virtio-gpu window and a virtio-mouse-device.
# This is the interactive desktop: click to raise windows, drag the
# title bar, click panel buttons, etc.
make qemu-fb-visible

# Run in QEMU with a USB host (qemu-xhci + usb-kbd + usb-mouse).
# The kernel prints "USB: controller initialized" and
# "USB: device on port ..." as it walks the xHCI root hub, then
# "USB: enumeration ok" and "USB HID: 2 devices" for the directly
# attached boot-protocol keyboard and mouse.
make qemu-usb

# Exit QEMU: Ctrl+A then X
```

### Debug with GDB

```bash
# Terminal 1: launch QEMU in debug mode (CPU paused)
make qemu-debug

# Terminal 2: attach GDB
gdb-multiarch build/kernel.elf
(gdb) target remote :1234
(gdb) break kernel_main
(gdb) continue
```

---

## Project Structure

```
kolibriarm/
├── boot/               # Bootloader (AArch64 ASM only)
│   └── start.S         # Entry point, early stack, BSS clear, jump to kernel
├── kernel/             # Kernel core (C + inline ASM)
│   ├── kernel.c        # kernel_main, early init
│   ├── mm/             # Memory management
│   ├── sched/          # Scheduler
│   ├── ipc.c           # Fixed-message IPC queue
│   └── gui_*.c         # Kernel-managed GUI modules
├── drivers/            # Hardware drivers (C + minimal ASM)
│   ├── board.h         # Generic board/platform interface
│   ├── boards/         # Board/platform glue, starting with qemu_virt
│   ├── uart/           # PL011 UART
│   ├── fb/             # Linear framebuffer primitives
│   ├── irq/            # GICv2 interrupt controller
│   ├── usb/            # USB HID (keyboard, mouse)
│   └── net/            # Ethernet driver
├── programs/           # Userland applications
├── docs/               # Focused project notes
│   ├── CURRENT_STATE.md
│   ├── GUI_ABI_NOTES.md
│   └── TECH_DEBT_REVIEW.md
├── ARCHITECTURE.md     # Kernel architecture overview
├── SYSCALLS.md         # Syscall ABI reference
├── MEMORY_MAP.md       # Fixed address/layout reference
├── PORTING.md          # Board porting notes
├── linker.ld           # Linker script
├── Makefile            # Build system
├── ROADMAP.md          # Development roadmap
├── CONTRIBUTING.md     # Contribution guidelines
└── LICENSE             # GPL-2.0
```

---

## Design Decisions

### Why AArch64?

AArch64 (ARM64) is the cleanest ISA to write an OS in from scratch:
- 31 general-purpose registers (no x86 legacy baggage)
- Clean, orthogonal instruction set
- Well-documented exception model (EL0/EL1/EL2)
- MMU with 4-level page tables, standard and predictable
- No real mode, no A20 gate, no interrupt legacy hell

### Why no POSIX?

POSIX compatibility layers add complexity without adding value for a purpose-built OS. KolibriARM defines its own minimal syscall ABI (inspired by KolibriOS's ~100-syscall design), using the `svc` instruction with function number in `x8` and arguments in `x0`–`x6`. This keeps the kernel small and the syscall path fast.

### Why C + ASM and not Rust?

The goal is to keep the codebase readable to anyone who knows C and assembly. Rust would add compile-time safety guarantees but also toolchain complexity, longer onboarding, and a runtime (even `no_std` has one). The entire kernel is meant to be understandable — Rust's borrow checker, while valuable, works against that goal at this stage.

---

## License

KolibriARM is licensed under the [GNU General Public License v2.0](LICENSE), the same license as KolibriOS.

---

## Acknowledgements

- [KolibriOS](https://kolibrios.org) — direct inspiration for philosophy and design
- [MenuetOS](https://www.menuetos.net/) — the original single-file OS concept
- [Raspberry Pi bare metal examples](https://github.com/isometimes/rpi4-osdev) — hardware reference
- [OSDev Wiki](https://wiki.osdev.org) — the indispensable community reference
