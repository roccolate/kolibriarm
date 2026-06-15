# Architecture

This document describes the technical architecture of KolibriARM. It is the reference for anyone writing kernel code or drivers.

Some sections describe the target architecture, not only the code that exists
today. When the current implementation is still a smaller bootstrap version,
the section calls that out explicitly.

---

## System Overview

KolibriARM is a **monolithic kernel** with a flat privilege model:

- **EL1** — kernel mode. All kernel code, drivers, and services run here.
- **EL0** — user mode. All application code runs here.
- **EL2/EL3** — not used (QEMU drops us into EL1 at boot; EL3 is firmware only).

There is no microkernel, no driver isolation, no capability system. This is intentional. Complexity is the enemy. Every driver that crashes takes the system down — that's acceptable for a compact OS where the driver code is auditable.

---

## Memory Layout

Current implementation note: the kernel still runs from an identity-mapped
lower-half address space while the EL0 demos use per-process `TTBR0_EL1` page
tables. The higher-half `TTBR1_EL1` layout below is the target layout for the
next stage of VM cleanup.

```
Virtual Address Space (AArch64, 48-bit, 4KB granule)
──────────────────────────────────────────────────────────────────
0x0000_0000_0000_0000  ┌─────────────────────────────┐
                       │  User space                  │
                       │  (per-process, TTBR0_EL1)   │
                       │  grows upward                │
0x0000_7FFF_FFFF_FFFF  └─────────────────────────────┘
                       [canonical hole — invalid addresses]
0xFFFF_0000_0000_0000  ┌─────────────────────────────┐
                       │  Kernel space (TTBR1_EL1)   │
                       │                              │
0xFFFF_0000_0040_0000  │  .text  (kernel code)        │
                       │  .rodata                     │
                       │  .data                       │
                       │  .bss                        │
0xFFFF_0000_0080_0000  │  kernel heap (kmalloc)       │
                       │  grows upward                │
0xFFFF_0000_4000_0000  │  Physical memory map         │
                       │  (identity-mapped MMIO)      │
0xFFFF_FFFF_FFFF_FFFF  └─────────────────────────────┘

Physical Memory (QEMU virt, 128 MB default)
──────────────────────────────────────────────────────────────────
0x0000_0000  ┌──────────────────┐
             │  DTB (device     │  placed here by QEMU firmware
0x0000_4000  │  tree blob)      │
             ├──────────────────┤
0x0004_0000  │  Bootloader      │  start.S entry point
             ├──────────────────┤
0x0040_0000  │  Kernel image    │  .text, .data, .bss
             ├──────────────────┤
0x0080_0000  │  PMM bitmap      │  tracks free/used frames
             ├──────────────────┤
0x0100_0000  │  Free frames     │  managed by PMM
             │  (available to   │
             │   kernel + user) │
0x0800_0000  └──────────────────┘  (128 MB = 0x0800_0000)

MMIO Regions (QEMU virt machine)
──────────────────────────────────────────────────────────────────
0x0900_0000   PL011 UART0
0x0800_0000   GIC-400 Distributor
0x0801_0000   GIC-400 CPU Interface
0x0A00_0000   virtio-blk (disk)
0x0A00_1000   virtio-net
0x0A00_2000   virtio-gpu
```

---

## Boot Sequence

```
QEMU firmware
    │
    │  Loads kernel.elf at 0x40000000
    │  Sets up DTB at 0x40000
    │  Drops into EL1
    ▼
start.S  (_start)
    │
    ├─ Check MPIDR: only core 0 continues
    ├─ Zero BSS section
    ├─ Set stack pointer to _start (grows downward from 0x40000000)
    ├─ Clear all GPRs
    └─ bl kernel_main
           │
           ▼
kernel_main() [kernel.c]
    │
    ├─ uart_init()          — configure PL011
    ├─ dtb_parse()          — read memory map from device tree
    ├─ pmm_init()           — initialize physical memory bitmap
    ├─ vmm_init()           — build page tables, enable MMU
    ├─ gic_init()           — initialize interrupt controller
    ├─ timer_init()         — configure ARM Generic Timer
    ├─ sched_init()         — initialize scheduler
    ├─ driver_init()        — probe and init registered drivers
    └─ sched_start()        — enable interrupts, start first process
```

---

## Physical Memory Manager (PMM)

Simple bitmap allocator. One bit per 4 KB page frame.

```c
// kernel/mm/pmm.h

#define PAGE_SIZE   4096
#define PAGE_SHIFT  12

void     pmm_init(uint64_t mem_start, uint64_t mem_size);
uint64_t pmm_alloc_page(void);          // returns physical address
void     pmm_free_page(uint64_t paddr);
uint64_t pmm_free_count(void);
```

The bitmap itself lives at a fixed physical address just above the kernel image. At 128 MB / 4 KB per page = 32768 pages = 4 KB of bitmap. Negligible.

**Allocation strategy:** First-fit scan from the start of the bitmap. Fast enough for the scale of this OS. A buddy allocator is future work if fragmentation becomes a problem.

---

## Virtual Memory Manager (VMM)

AArch64 uses a 4-level page table (when using 48-bit VA and 4 KB granule):

```
VA[47:39]  →  PGD (Page Global Directory)   level 0
VA[38:30]  →  PUD (Page Upper Directory)    level 1
VA[29:21]  →  PMD (Page Middle Directory)   level 2
VA[20:12]  →  PTE (Page Table Entry)        level 3
VA[11:0]   →  Offset within page
```

Each process has its own PGD (physical address stored in `TTBR0_EL1`). The
current QEMU bootstrap also maps the kernel and MMIO into each process table so
EL1 can keep running after exceptions. The target design is to keep the kernel
page table shared across all processes via `TTBR1_EL1`.

```c
// kernel/mm/vmm.h

int      vmm_map_page(uint64_t *pgd, uint64_t vaddr, uint64_t paddr, uint64_t flags);
int      vmm_map_range(uint64_t *pgd, uint64_t vaddr, uint64_t paddr,
                       uint64_t size, uint64_t flags);
int      vmm_unmap_page(uint64_t *pgd, uint64_t vaddr);
int      vmm_unmap_range(uint64_t *pgd, uint64_t vaddr, uint64_t size);
uint64_t vmm_virt_to_phys(uint64_t *pgd, uint64_t vaddr);
uint64_t *vmm_new_table(void);          // allocates and zeros a PGD
// Future: vmm_free_table(uint64_t *pgd) will free all levels owned by a process.
```

**Page flags (PTE bits):**
```
Bit 0:    Valid
Bit 1:    Table/page (1 = page at level 3)
Bit 6:    AP[1] — 0=EL1 only, 1=EL0+EL1
Bit 7:    AP[2] — 0=read-write, 1=read-only
Bit 10:   AF  — access flag (must be set or fault on first access)
Bit 54:   XN  — execute never
```

---

## Process Model

Each process is described by a Process Control Block:

```c
// kernel/sched/process.h

#define PROC_RUNNING  0
#define PROC_READY    1
#define PROC_BLOCKED  2
#define PROC_ZOMBIE   3

typedef struct process {
    uint32_t  pid;
    uint32_t  state;

    // Saved CPU context (filled on context switch)
    uint64_t  regs[31];     // x0–x30
    uint64_t  sp_el0;       // user stack pointer
    uint64_t  elr_el1;      // return address (PC to restore)
    uint64_t  spsr_el1;     // saved program status register

    // Memory
    uint64_t *pgd;          // physical address of PGD
    uint64_t  heap_base;
    uint64_t  heap_top;

    // Scheduling
    uint32_t  priority;     // 0 (lowest) – 7 (highest)
    uint32_t  ticks;        // remaining time slice

    // Linked list
    struct process *next;
    struct process *prev;
} process_t;
```

**Context switch** is implemented in `kernel/sched/switch.S`:

```asm
// switch_context(process_t *old, process_t *new)
// Saves old, loads new, switches TTBR0_EL1
switch_context:
    stp x0,  x1,  [x0, #0]
    stp x2,  x3,  [x0, #16]
    // ... save all registers ...
    mrs x9, elr_el1
    str x9, [x0, #PROC_ELR_OFFSET]
    // ... switch to new process page table ...
    msr ttbr0_el1, x10
    isb
    // ... restore new registers ...
    eret
```

---

## System Call ABI

System calls use the `svc #0` instruction. The convention:

| Register | Role                        |
|----------|-----------------------------|
| `x8`     | Syscall number              |
| `x0`–`x5`| Arguments (up to 6)        |
| `x0`     | Return value                |
| `x1`     | Error code (if x0 = -1)     |

The kernel's `svc` handler (in `boot/vectors.S`) saves the full register context, dispatches through a jump table indexed by `x8`, and restores context on return.

See [SYSCALLS.md](SYSCALLS.md) for the full syscall reference.

---

## Driver Model

Drivers are static — registered at compile time, not loaded dynamically. Each driver implements a simple interface:

```c
// kernel/drivers.h

typedef struct driver {
    const char *name;
    int (*probe)(void);       // returns 0 if hardware found
    int (*init)(void);        // initialize hardware
    void (*shutdown)(void);
} driver_t;

// Registration macro (used in driver source files)
#define REGISTER_DRIVER(d)  \
    static driver_t *__driver_##d \
    __attribute__((section(".drivers"))) = &d;
```

At boot, `driver_init()` iterates the `.drivers` section and calls `probe()` + `init()` for each registered driver in order.

---

## Interrupt Handling

The AArch64 exception vector table has 16 entries (4 types × 4 privilege levels). KolibriARM only uses the subset relevant to EL1:

```
Offset 0x200:  IRQ from EL0  (user process interrupted)
Offset 0x280:  FIQ from EL0
Offset 0x400:  IRQ from EL1  (kernel interrupted — rare)
Offset 0x600:  SError        (system error, fatal)
```

The vector table lives in `boot/vectors.S` and is registered via:
```asm
adr x0, vector_table
msr vbar_el1, x0
```

Each IRQ handler saves context, calls the C-level `irq_dispatch(irq_number)`, which routes to the appropriate driver's interrupt service routine.

---

## GUI Architecture

The GUI runs entirely in kernel space (EL1), following the KolibriOS model. There is no display server process.

```
┌──────────────────────────────────────────────┐
│  Application (EL0)                           │
│  draws into its own window buffer            │
│  calls sys_window_flush() when done          │
└──────────────────┬───────────────────────────┘
                   │ syscall
┌──────────────────▼───────────────────────────┐
│  Kernel Window Manager (EL1)                 │
│  maintains z-order stack                     │
│  composites dirty regions to framebuffer     │
│  routes input events to focused window       │
└──────────────────┬───────────────────────────┘
                   │ DMA / direct write
┌──────────────────▼───────────────────────────┐
│  Linear Framebuffer (physical MMIO)          │
│  ARGB8888, width × height × 4 bytes          │
└──────────────────────────────────────────────┘
```

This eliminates context switch overhead for every draw call. The tradeoff is that a buggy application can corrupt the display — acceptable for this OS's use case.

---

## Coding Standards

- C11, no C++ (exception: `//` comments are fine)
- No libc headers. Ever. Use `kernel/lib/` replacements.
- `snake_case` for functions and variables
- `UPPER_SNAKE_CASE` for constants and macros
- Every public function has a doc comment in its `.h` file
- No global mutable state outside of explicitly named `g_` prefixed variables
- Assembly files use `.S` extension (uppercase, enables C preprocessor)
- All assembly functions callable from C must follow the AAPCS64 calling convention

---

## References

- [ARM Architecture Reference Manual (ARMv8, for A-profile)](https://developer.arm.com/documentation/ddi0487)
- [QEMU virt machine memory map](https://github.com/qemu/qemu/blob/master/hw/arm/virt.c)
- [AArch64 bare metal guide — s-matyukevich](https://github.com/s-matyukevich/raspberry-pi-os)
- [OSDev Wiki — ARM](https://wiki.osdev.org/ARM)
- [GIC-400 TRM](https://developer.arm.com/documentation/ddi0471)
