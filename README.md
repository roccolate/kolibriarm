# KolibriARM

> A minimal, fast, assembly-first operating system for AArch64 — inspired by KolibriOS and MenuetOS.

[![License: GPL-2.0](https://img.shields.io/badge/License-GPL%202.0-blue.svg)](LICENSE)
[![Architecture](https://img.shields.io/badge/arch-AArch64-green.svg)]()
[![Language](https://img.shields.io/badge/lang-C%20%2B%20ASM-orange.svg)]()
[![Status](https://img.shields.io/badge/status-pre--alpha-red.svg)]()

---

## What is KolibriARM?

KolibriARM is a bare-metal operating system for ARM64 (AArch64) processors, written entirely in C and AArch64 assembly. It takes direct inspiration from [KolibriOS](https://kolibrios.org) and [MenuetOS](https://www.menuetos.net/) — two x86 operating systems celebrated for their extreme compactness, speed, and elegance — and brings those principles to modern ARM hardware.

**Core philosophy:**
- Every byte is intentional. No unnecessary abstraction layers.
- The kernel fits in your head. Small enough to read in a weekend.
- No libc. No POSIX. No Linux compatibility layer. Just clean system calls.
- Boots in under 3 seconds on real hardware.
- Runs comfortably on 64 MB of RAM.

---

## Current Status

> **Pre-alpha.** The project is in active early development. Only the boot sequence and UART output are functional.

| Component         | Status       | Notes                          |
|-------------------|-------------|--------------------------------|
| Bootloader        | In progress  | AArch64 ASM, QEMU virt tested  |
| Physical memory   | Planned      | Bitmap allocator               |
| Virtual memory    | Planned      | 4-level page tables (ARMv8)    |
| Scheduler         | Planned      | Preemptive, single core first  |
| UART driver       | Working      | PL011, QEMU virt               |
| Framebuffer       | Planned      | Linear framebuffer via DTB     |
| Filesystem        | Planned      | FAT32 read-only first          |
| GUI               | Planned      | Custom compositor, no X11      |
| Networking        | Planned      | lwIP integration               |

---

## Target Hardware

**Primary target:** QEMU `virt` machine (AArch64, Cortex-A72)
**Secondary target:** Raspberry Pi 4 / 5 (BCM2711 / BCM2712)
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
git clone https://github.com/yourname/kolibriarm
cd kolibriarm

# Build
make

# Run in QEMU
make qemu

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
│   └── start.S         # Entry point, MMU init, jump to kernel
├── kernel/             # Kernel core (C + inline ASM)
│   ├── kernel.c        # kernel_main, early init
│   ├── mm/             # Memory management
│   ├── sched/          # Scheduler
│   └── ipc/            # System calls and IPC
├── drivers/            # Hardware drivers (C + minimal ASM)
│   ├── uart/           # PL011 UART
│   ├── display/        # Framebuffer / DRM
│   ├── usb/            # USB HID (keyboard, mouse)
│   └── net/            # Ethernet driver
├── programs/           # Userland applications
├── docs/               # Documentation
│   ├── ARCHITECTURE.md
│   ├── SYSCALLS.md
│   ├── MEMORY_MAP.md
│   └── PORTING.md
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

POSIX compatibility layers add complexity without adding value for a purpose-built OS. KolibriARM defines its own minimal syscall ABI (inspired by KolibriOS's ~100-syscall design), using the `svc` instruction with function number in `x8` and arguments in `x0`–`x7`. This keeps the kernel small and the syscall path fast.

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
