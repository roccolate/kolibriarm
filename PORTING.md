# Porting Guide

KolibriARM is designed so that porting to a new ARM64 board requires changing **only the drivers** — the kernel, scheduler, memory manager, and syscall layer are board-agnostic.

---

## Architecture Goal

KolibriARM should be structured so that the following components remain portable and avoid board-specific constants:

- `kernel/mm/` — physical and virtual memory management
- `kernel/sched/` — scheduler and context switch
- `kernel/ipc/` — message passing and shared memory
- `kernel/fs/` — VFS and filesystem drivers
- `kernel/gui/` — window manager and compositor
- `boot/start.S` — except the initial stack address (see below)

The following components are board-specific and should live behind `drivers/boards/<board>/` or an equivalent platform layer:

- UART (base address, register layout)
- Interrupt controller (GIC version and base address)
- Timer (system timer base address)
- Display (framebuffer address, resolution query method)
- Storage (eMMC, NVMe, or virtio)
- USB host controller
- Network controller

**Current note:** QEMU `virt` is the reference board under `drivers/boards/qemu_virt/`. Its board layer owns the early UART init, GIC init, UART IRQ number, and MMIO mappings needed by the identity page table.

**USB note:** the active USB backend is the generic PCI xHCI driver under
`drivers/usb/`. QEMU `virt` reaches it through ECAM + PCI BAR assignment.
Raspberry Pi 4 still needs BCM2711 PCIe host bridge setup in
`drivers/boards/rpi4/` before the VL805 xHCI controller can be discovered.

**Next portability step:** keep moving new board-specific devices behind `drivers/boards/<board>/` helpers instead of adding raw MMIO addresses to generic kernel code.

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

### Step 2: Keep the board interface split

Generic kernel code includes `drivers/board.h`. A board-specific header under
`drivers/boards/<board>/board.h` should include that generic contract and keep
only private constants for the board implementation.

```c
// drivers/boards/your_board/board.h

#pragma once
#include <stdint.h>

#include "drivers/board.h"

// Private constants for this board implementation.
#define BOARD_UART_BASE     0x09000000ULL  // change for your board
#define BOARD_UART_IRQ      33             // GIC SPI number
#define BOARD_GIC_DIST_BASE 0x08000000ULL
#define BOARD_GIC_CPU_BASE  0x08010000ULL
```

### Step 3: Implement board.c

Every board must implement the functions declared by `drivers/board.h`:

```c
// drivers/boards/your_board/board.c

#include "boards/your_board/board.h"

#include "irq/gicv2.h"       // or your interrupt controller
#include "kernel/mm/vmm.h"
#include "uart/pl011.h"      // or your UART type

const char *board_name(void) {
    return "your_board";
}

void board_early_init(void) {
    uart_init(BOARD_UART_BASE);
}

int board_map_mmio(uint64_t *pgd) {
    return vmm_map_page(pgd, BOARD_UART_BASE, BOARD_UART_BASE,
                        VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_DEVICE);
}

void board_irq_init(void) {
    gicv2_init(BOARD_GIC_DIST_BASE, BOARD_GIC_CPU_BASE);
}

void board_irq_enable(uint32_t irq) {
    gicv2_enable_irq(irq);
}

uint32_t board_irq_ack(void) {
    return gicv2_ack_irq();
}

void board_irq_end(uint32_t irq) {
    gicv2_end_irq(irq);
}

int board_irq_is_spurious(uint32_t irq) {
    return irq == GIC_SPURIOUS_IRQ;
}

uint32_t board_uart0_irq(void) {
    return BOARD_UART_IRQ;
}

uint64_t board_virtio_mmio_base(void) {
    return 0;
}

uint64_t board_virtio_mmio_size(void) {
    return 0;
}

uint64_t board_virtio_mmio_stride(void) {
    return 0;
}
```

### Step 4: Select the board at build time

```bash
# qemu_virt is the default. Override it on the command line:
make BOARD=your_board
```

The Makefile compiles `drivers/boards/$(BOARD)/board.o`. Do not add the board
directory to the global include path unless there is a specific reason; generic
kernel files should keep resolving `#include "board.h"` to `drivers/board.h`.

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
- [ ] Verify `dtb_get_memory()` sees the RAM map, or add a board-specific fallback
- [ ] Verify PMM initializes without overwriting kernel or firmware regions

### Phase E: Display

**Option 1: Linear framebuffer (simplest)**
- Find framebuffer base address from DTB (`/chosen/linux,initrd-start` or `/framebuffer` node)
- Feed the resolved base address, dimensions, pitch, and pixel format into the framebuffer driver
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
| Raspberry Pi 4 | In Progress | Phase 8 target, board layer done |
| Raspberry Pi 5 | Planned | After RPi 4 |
| Orange Pi 5 (RK3588) | Future | After RPi port matures |

---

## Getting Help

If you're porting to a new board and get stuck:

1. Check if Linux has a DTS for your board — it contains all the MMIO addresses you need
2. Search the OSDev forums for your SoC name
3. Open a GitHub Discussion with your board name and the exact point where you're stuck
