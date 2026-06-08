# Porting Guide

KolibriARM is designed so that porting to a new ARM64 board requires changing **only the drivers** — the kernel, scheduler, memory manager, and syscall layer are board-agnostic.

---

## Architecture Guarantee

The following components are **fully portable** and must never contain board-specific code:

- `kernel/mm/` — physical and virtual memory management
- `kernel/sched/` — scheduler and context switch
- `kernel/ipc/` — message passing and shared memory
- `kernel/fs/` — VFS and filesystem drivers
- `kernel/gui/` — window manager and compositor
- `boot/start.S` — except the initial stack address (see below)

The following components are **board-specific** and live in `drivers/`:

- UART (base address, register layout)
- Interrupt controller (GIC version and base address)
- Timer (system timer base address)
- Display (framebuffer address, resolution query method)
- Storage (eMMC, NVMe, or virtio)
- USB host controller
- Network controller

---

## How to Add a New Board

### Step 1: Create a board directory

```
drivers/
└── boards/
    ├── qemu_virt/       ← reference implementation
    │   ├── board.h
    │   └── board.c
    ├── rpi4/
    │   ├── board.h
    │   └── board.c
    └── your_board/
        ├── board.h      ← you create this
        └── board.c
```

### Step 2: Implement board.h

Every board must define these constants and implement these functions:

```c
// drivers/boards/your_board/board.h

#pragma once
#include <stdint.h>

// ── Physical memory ───────────────────────────────────────────
#define BOARD_DRAM_BASE     0x00000000ULL  // where RAM starts
#define BOARD_DRAM_SIZE     0x08000000ULL  // total RAM size

// ── UART ──────────────────────────────────────────────────────
#define BOARD_UART_BASE     0x09000000ULL  // change for your board
#define BOARD_UART_IRQ      33             // GIC SPI number

// ── Interrupt controller ──────────────────────────────────────
#define BOARD_GIC_DIST_BASE 0x08000000ULL
#define BOARD_GIC_CPU_BASE  0x08010000ULL

// ── Timer ─────────────────────────────────────────────────────
#define BOARD_TIMER_FREQ    62500000ULL    // ARM Generic Timer freq

// ── Display ───────────────────────────────────────────────────
// Set to 0 if display must be queried at runtime (e.g. RPi mailbox)
#define BOARD_FB_BASE       0x00000000ULL
#define BOARD_FB_WIDTH      1920
#define BOARD_FB_HEIGHT     1080

// ── Board init ────────────────────────────────────────────────
void board_early_init(void);   // called before MMU is enabled
void board_init(void);         // called after MMU and PMM are ready
const char *board_name(void);  // returns "your_board_name"
```

### Step 3: Implement board.c

```c
// drivers/boards/your_board/board.c

#include "board.h"
#include <drivers/uart/pl011.h>   // or your UART type
#include <drivers/gic/gic400.h>   // or your GIC version

void board_early_init(void) {
    // Minimal init before the kernel is fully up.
    // Usually just: configure UART clock, set baud rate.
    pl011_init(BOARD_UART_BASE, 115200);
}

void board_init(void) {
    // Full driver init after MMU and heap are available.
    gic400_init(BOARD_GIC_DIST_BASE, BOARD_GIC_CPU_BASE);
    // ... storage, display, network ...
}

const char *board_name(void) {
    return "your_board v1.0";
}
```

### Step 4: Select the board at build time

```bash
# In Makefile or passed on command line:
make BOARD=your_board

# Makefile includes the right board:
BOARD_DIR = drivers/boards/$(BOARD)
CFLAGS   += -I$(BOARD_DIR)
```

---

## Porting Checklist

Work through this list in order. Each item depends on the previous.

### Phase A: Boot

- [ ] Identify where your board's boot ROM loads the kernel (load address)
- [ ] Update `linker.ld`: change `. = 0x40000000` to your load address
- [ ] Verify entry point: does your bootloader (U-Boot / firmware) jump to `_start`?
- [ ] Check initial exception level: are you at EL2 or EL1? (EL2 needs a drop)

**EL2 to EL1 drop (if needed, add to start.S):**
```asm
// Check current EL
mrs x0, CurrentEL
lsr x0, x0, #2
cmp x0, #2
bne .not_el2

// Drop to EL1
msr elr_el2, x9        // return address
mov x0, #0x3c5         // SPSR: EL1h, interrupts masked
msr spsr_el2, x0
eret
.not_el2:
```

### Phase B: UART Output

- [ ] Find your board's UART base address (datasheet / Linux DTS)
- [ ] Check UART type: PL011, 8250/16550, BCM mini UART, or custom
- [ ] Implement `uart_putc()` using the correct register offsets
- [ ] Verify: `make qemu` (or boot on hardware) prints the boot banner

This is your most important milestone. Nothing else matters until you have text output.

### Phase C: Interrupts

- [ ] Identify interrupt controller: GIC-400, GIC-500, or custom
- [ ] Find GIC distributor and CPU interface base addresses
- [ ] Initialize the GIC (enable, set priority mask, enable CPU interface)
- [ ] Verify timer interrupt fires at the expected rate

### Phase D: Memory

- [ ] Find the board's RAM size (from DTB, firmware API, or hardcoded)
- [ ] Update `BOARD_DRAM_BASE` and `BOARD_DRAM_SIZE`
- [ ] Verify PMM initializes without overwriting kernel or firmware regions

### Phase E: Display

**Option 1: Linear framebuffer (simplest)**
- Find framebuffer base address from DTB (`/chosen/linux,initrd-start` or `/framebuffer` node)
- Set `BOARD_FB_BASE`, `BOARD_FB_WIDTH`, `BOARD_FB_HEIGHT`
- No additional driver code needed

**Option 2: Raspberry Pi mailbox (RPi 4/5)**
```c
// Query framebuffer from VideoCore firmware via mailbox
uint32_t mailbox_fb_init(uint32_t width, uint32_t height) {
    // ... mailbox property interface ...
    // Returns physical address of framebuffer
}
```

**Option 3: DRM/KMS (most capable, most complex)**
- Parse DRM device from DTB
- Implement minimal CRTC setup for your display controller
- Recommended only for boards with open DRM drivers (Allwinner SUNXI, etc.)

### Phase F: Storage

**virtio-blk (QEMU only):**
Already implemented. No changes needed.

**eMMC / SD (RPi 4):**
```c
// Implement in drivers/storage/emmc.c
int emmc_init(uint64_t base);
int emmc_read(uint32_t lba, uint32_t count, void *buf);
int emmc_write(uint32_t lba, uint32_t count, const void *buf);
```

---

## Device Tree (DTB)

QEMU and most ARM boards pass a Device Tree Blob (DTB) to the kernel. KolibriARM reads the DTB to find:

- Available RAM regions
- Framebuffer address and size
- UART base address and IRQ

The DTB parser lives in `kernel/dtb.c`. It implements a minimal subset of the FDT spec:

```c
uint64_t dtb_get_mem_size(void *dtb);
uint64_t dtb_get_prop_u64(void *dtb, const char *path, const char *prop);
```

When porting to a board that doesn't provide a DTB (rare for Cortex-A), hardcode the values in `board.h` and stub out the DTB parser.

---

## Tested Boards

| Board | Status | Notes |
|-------|--------|-------|
| QEMU virt (Cortex-A72) | Working | Primary development target |
| Raspberry Pi 4 | Planned | Phase 8 target |
| Raspberry Pi 5 | Planned | After RPi 4 |
| Orange Pi 5 (RK3588) | Future | After RPi port matures |

---

## Getting Help

If you're porting to a new board and get stuck:

1. Check if Linux has a DTS for your board — it contains all the MMIO addresses you need
2. Search the OSDev forums for your SoC name
3. Open a GitHub Discussion with your board name and the exact point where you're stuck
