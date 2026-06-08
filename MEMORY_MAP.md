# Memory Map

This document is the authoritative reference for all fixed addresses and memory regions in KolibriARM.

---

## Physical Memory (QEMU virt, 128 MB)

| Start        | End          | Size    | Contents                          |
|--------------|--------------|---------|-----------------------------------|
| `0x00000000` | `0x0003FFFF` | 256 KB  | Reserved (DTB, firmware)          |
| `0x00040000` | `0x000FFFFF` | 768 KB  | Bootloader (`start.S`)            |
| `0x00400000` | `0x007FFFFF` | 4 MB    | Kernel image (.text .data .bss)   |
| `0x00800000` | `0x00800FFF` | 4 KB    | PMM bitmap (32768 bits = 128 MB / 4 KB) |
| `0x00801000` | `0x00FFFFFF` | ~8 MB   | Kernel heap (`kmalloc` pool)      |
| `0x01000000` | `0x07FFFFFF` | ~112 MB | Free frames (user processes)      |

---

## MMIO Regions (QEMU virt machine)

These addresses are defined by QEMU's `virt` machine and must not be used for anything else.

| Address      | Device                     | Notes                           |
|--------------|----------------------------|---------------------------------|
| `0x09000000` | PL011 UART0                | Primary console                 |
| `0x09010000` | PL011 UART1                | Reserved                        |
| `0x08000000` | GIC-400 Distributor        | Interrupt controller            |
| `0x08010000` | GIC-400 CPU Interface      | Per-core interrupt interface    |
| `0x08030000` | GIC-400 Hypervisor         | Not used (EL2 only)             |
| `0x0A000000` | virtio-mmio (disk)         | virtio-blk block device         |
| `0x0A001000` | virtio-mmio (net)          | virtio-net network device       |
| `0x0A002000` | virtio-mmio (GPU)          | virtio-gpu display device       |
| `0x0A003000` | virtio-mmio (input)        | virtio-input keyboard/mouse     |
| `0x09020000` | PL031 RTC                  | Real-time clock                 |
| `0x09030000` | PL061 GPIO                 | Not used in virt                |

---

## Virtual Address Space

### Kernel Space (`TTBR1_EL1` — shared across all processes)

| Virtual Address          | Maps to (physical)     | Contents                     |
|--------------------------|------------------------|------------------------------|
| `0xFFFF000000400000`     | `0x00400000`           | Kernel .text                 |
| `0xFFFF000000600000`     | `0x00600000`           | Kernel .rodata               |
| `0xFFFF000000700000`     | `0x00700000`           | Kernel .data + .bss          |
| `0xFFFF000000800000`     | `0x00800000`           | PMM bitmap                   |
| `0xFFFF000000801000`     | `0x00801000`           | Kernel heap                  |
| `0xFFFF000009000000`     | `0x09000000`           | UART0 MMIO (identity)        |
| `0xFFFF000008000000`     | `0x08000000`           | GIC MMIO (identity)          |
| `0xFFFF00000A000000`     | `0x0A000000`           | virtio MMIO (identity)       |

The kernel identity-maps all MMIO at the same offset (`0xFFFF0000_XXXXXXXX`).
Access any device at `DEVICE_VIRT = DEVICE_PHYS | 0xFFFF000000000000`.

### User Space (`TTBR0_EL1` — per-process)

| Virtual Address          | Contents                                  |
|--------------------------|-------------------------------------------|
| `0x0000000000010000`     | Process .text (loaded here by exec)       |
| `0x0000000000100000`     | Process .data and .bss                    |
| `0x0000000000200000`     | Process heap (grows up)                   |
| `0x0000007FFFFF0000`     | User stack (grows down from top)          |

The user address space top is `0x0000_7FFF_FFFF_FFFF` (canonical 48-bit limit).
The kernel never touches TTBR0 of another process.

---

## Stack Layout

### Kernel stack (per-process, in kernel heap)

Each process has a 16 KB kernel stack allocated at process creation.
The kernel stack is used only during syscalls and interrupt handling.

```
High address  ┌──────────────────────┐  ← initial SP (aligned to 16 bytes)
              │  exception frame     │  ← saved on each exception entry
              │  local variables     │
              │  ...                 │
Low address   └──────────────────────┘  ← stack guard page (unmapped)
```

### User stack

Default size: 1 MB, placed at the top of user space.
Grows downward. The page below the bottom is unmapped (stack overflow detection via page fault).

---

## Page Table Layout

4-level page tables, 4 KB granule, 48-bit virtual addresses.

```
Each table: 512 entries × 8 bytes = 4 KB (one page)

PGD[VA[47:39]]  →  PUD table physical address
PUD[VA[38:30]]  →  PMD table physical address
PMD[VA[29:21]]  →  PTE table physical address
PTE[VA[20:12]]  →  Physical page address + flags

VA[11:0]  →  Byte offset within page (4 KB)
```

Page table entries use the ARMv8-A descriptor format. See `docs/ARCHITECTURE.md` for the flag bits.

---

## Raspberry Pi 4 Physical Memory (for reference)

When porting to real hardware, the key differences from QEMU virt:

| Region       | QEMU virt      | RPi 4 (BCM2711) |
|--------------|----------------|-----------------|
| DRAM start   | `0x40000000`   | `0x00000000`    |
| DRAM size    | Configurable   | 1/2/4/8 GB      |
| UART base    | `0x09000000`   | `0xFE201000`    |
| GIC base     | `0x08000000`   | `0xFF841000`    |
| Framebuffer  | virtio-gpu     | VC4 mailbox     |
| SD/eMMC      | virtio-blk     | `0xFE340000`    |

The driver abstraction layer in `drivers/` is designed so only the base address constants change between targets.
