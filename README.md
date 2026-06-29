# ArmoniOS

> A minimal, fast, assembly-first operating system for AArch64 — inspired by KolibriOS and MenuetOS.

[![License: GPL-2.0](https://img.shields.io/badge/License-GPL%202.0-blue.svg)](LICENSE)
[![Architecture](https://img.shields.io/badge/arch-AArch64-green.svg)]()
[![Language](https://img.shields.io/badge/lang-C%20%2B%20ASM-orange.svg)]()
[![Status](https://img.shields.io/badge/status-QEMU%20desktop-blue.svg)]()

---

## A Tiny Desktop OS For ARM

ArmoniOS is a small bare-metal operating system for ARM64. It boots directly
into a graphical desktop on QEMU: a panel, a shell, an editor, a file manager,
a monitor, a clock, mouse input, windows, process isolation, storage, USB HID,
and virtio-net DHCP.

The project is inspired by [KolibriOS](https://kolibrios.org) and
[MenuetOS](https://www.menuetos.net/): compact systems where the whole OS is
small enough to understand, fast enough to feel immediate, and direct enough
that every subsystem has a visible purpose.

ArmoniOS is not a Linux clone. There is no libc, no POSIX layer, and no hosted
runtime. User programs are freestanding AArch64 images that talk to the kernel
through a small `svc #0` syscall ABI.

## Try The Desktop

```bash
git clone https://github.com/roccolate/armonios
cd armonios
make
make qemu-fb-visible
```

In the visible QEMU window you can:

- click panel buttons to launch or focus `shell`, `editor`, `files`,
  `monitor`, and `clock`;
- drag windows by their title bars;
- type into the editor;
- browse and manage FAT32 root files through `files` when a FAT disk is
  present;
- use the shell to run apps, list processes, inspect memory, and kill the last
  spawned process.

For a quick headless smoke test:

```bash
timeout 25s make qemu-fb
```

Success means the serial log reaches `panel: ready`.

---

## What Works Today

ArmoniOS is currently at the **v0.9 QEMU desktop baseline**. The next target is
**v1.0: a stable, repeatable QEMU desktop release**.

Latest verified size: `kernel.bin: 92696 bytes (limit: 100000)`.

Highlights:

- AArch64 boot path, MMU setup, physical/virtual memory managers.
- Preemptive scheduling with kernel threads and EL0 user processes.
- Per-process address spaces, user stacks, and KLI1 flat app images.
- A kernel-owned GUI compositor with windows, focus, dragging, title bars,
  cursor regions, backing buffers, and damage-rectangle redraw.
- Freestanding C apps under `programs/apps/`: `panel`, `shell`, `editor`,
  `files`, `monitor`, and `clock`.
- VFS with bootfs, tmpfs, and FAT32 integration through virtio-blk; `SYS_OPEN`
  supports `O_CREAT` for FAT32 root files under `/fat/<8.3-name>`.
- Input through virtio-input and USB HID keyboard/mouse paths.
- virtio-net initialization and DHCP on QEMU.
- Host tests for memory, scheduler, process isolation, syscalls, GUI ABI,
  FAT32, DHCP options, USB/HID parsing, and app-image layout.

Release gates currently used:

```bash
make
make size
make -C tests test
make stack-check
make qemu-fs-test
timeout 25s make qemu-fb
timeout 25s make qemu-usb
timeout 25s make qemu-net
```

See [docs/ROADMAP.md](docs/ROADMAP.md) for the version plan and remaining
work.

---

## Philosophy

- Keep the kernel small enough to read in one sitting.
- Prefer direct C and small AArch64 assembly boundaries over large frameworks.
- Make every byte earn its place; the kernel binary has a tight size gate.
- Keep QEMU fast, observable, and repeatable before claiming hardware support.
- Port ideas from classic small OSes, not their x86 assembly.

## Not Yet

These are planned or future work, not current release claims:

- Raspberry Pi 4 real-hardware boot.
- USB hub support.
- TCP/HTTP clients.
- SMP and secondary-core startup.
- Engine and multimedia runtime.

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
git clone https://github.com/roccolate/armonios
cd armonios

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
# pwd
# cd /fat
# ls
# ls /fat
# cat /tmp/note
# ps
# ticks
# mem
# run editor
# run files
# run editor myfile.txt    # passes myfile.txt as argv[1] to the editor
# run editor /fat/NOTE.TXT # opens or creates a FAT32 root file
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

# Run in QEMU with a visible virtio-gpu window, USB keyboard, and virtio mouse.
# This is the interactive desktop: click to raise windows, type into
# focused apps, drag title bars, click panel buttons, etc.
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
armonios/
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
│   ├── ARCHITECTURE.md
│   ├── CONTRIBUTING.md
│   ├── CURRENT_STATE.md
│   ├── ENGINE_MULTIMEDIA.md
│   ├── GUI_ABI_NOTES.md
│   ├── MEMORY_MAP.md
│   ├── PORTING.md
│   ├── ROADMAP.md
│   ├── SYSCALLS.md
│   └── TECH_DEBT_REVIEW.md
├── linker/             # Kernel linker scripts
│   ├── linker.ld       # QEMU kernel layout
│   └── linker_rpi4.ld  # Raspberry Pi 4 kernel layout
├── Makefile            # Build system
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

POSIX compatibility layers add complexity without adding value for a
purpose-built OS. ArmoniOS defines its own minimal syscall ABI (inspired by
KolibriOS's ~100-syscall design), using the `svc` instruction with function
number in `x8` and arguments in `x0`–`x6`. This keeps the kernel small and the
syscall path fast.

### Why C + ASM and not Rust?

The goal is to keep the codebase readable to anyone who knows C and assembly. Rust would add compile-time safety guarantees but also toolchain complexity, longer onboarding, and a runtime (even `no_std` has one). The entire kernel is meant to be understandable — Rust's borrow checker, while valuable, works against that goal at this stage.

---

## License

ArmoniOS is licensed under the [GNU General Public License v2.0](LICENSE), the
same license as KolibriOS.

---

## Acknowledgements

- [KolibriOS](https://kolibrios.org) — direct inspiration for philosophy and design
- [MenuetOS](https://www.menuetos.net/) — the original single-file OS concept
- [Raspberry Pi bare metal examples](https://github.com/isometimes/rpi4-osdev) — hardware reference
- [OSDev Wiki](https://wiki.osdev.org) — the indispensable community reference
