# Roadmap

This document describes the planned development trajectory for KolibriARM. Each phase has clear entry/exit criteria — a phase is "done" only when all its criteria are met and verified in QEMU.

---

## Phase 0 — Foundation `[current]`

**Goal:** A binary that boots, prints to UART, and halts cleanly.

**Scope:**
- AArch64 bootloader in pure ASM (`boot/start.S`)
- MMU left disabled (identity mapping)
- BSS zeroing
- Stack initialization
- Jump to `kernel_main()` in C
- UART PL011 driver (polling, no interrupts)
- Linker script placing code at `0x40000000`
- Makefile with `make`, `make qemu`, `make qemu-debug` targets

**Exit criteria:**
- [ ] `make qemu` prints a boot message to the terminal
- [ ] GDB can attach, set a breakpoint at `kernel_main`, and step through C code
- [ ] Binary size under 32 KB

**Estimated effort:** 1–2 weeks

---

## Phase 1 — Memory Management

**Goal:** The kernel can allocate and free physical memory pages, and map virtual memory with the MMU enabled.

### 1.1 Physical Memory Manager (PMM)
- Bitmap allocator over all available RAM
- `pmm_alloc_page()` → returns a 4 KB physical frame
- `pmm_free_page(addr)` → marks frame as free
- Memory map parsed from DTB (Device Tree Blob) provided by QEMU

### 1.2 Virtual Memory Manager (VMM)
- Enable MMU at EL1
- 4-level page tables (PGD → PUD → PMD → PTE), 4 KB granule
- Identity map the kernel (physical == virtual for kernel space)
- Separate user address space (0x0000_0000_0000_0000 – 0x0000_FFFF_FFFF_FFFF)
- `vmm_map(vaddr, paddr, flags)` — map a virtual page to a physical frame
- `vmm_unmap(vaddr)` — unmap and free

### 1.3 Kernel Heap
- Simple slab allocator or bump allocator
- `kmalloc(size)` / `kfree(ptr)`
- Used internally by the kernel only (no userland malloc yet)

**Exit criteria:**
- [ ] MMU enabled, kernel running with virtual addresses
- [ ] `pmm_alloc_page()` returns unique non-overlapping frames
- [ ] `kmalloc(64)` works without crashing
- [ ] All tests pass in QEMU

**Estimated effort:** 3–5 weeks

---

## Phase 2 — Processes and Scheduler

**Goal:** Multiple independent processes can run concurrently, each in its own address space.

### 2.1 Process Control Block (PCB)
```c
typedef struct process {
    uint64_t  pid;
    uint64_t  regs[31];      // x0–x30
    uint64_t  sp, pc, pstate;
    uint64_t *page_table;    // PGD physical address
    int       state;         // RUNNING, READY, BLOCKED, ZOMBIE
    struct process *next;
} process_t;
```

### 2.2 Context Switch (ASM)
- Save/restore all AArch64 registers
- Switch page tables via `TTBR0_EL1`
- Implemented in ASM for correctness and speed

### 2.3 Preemptive Scheduler
- ARM Generic Timer (CNTV_EL0) as tick source
- Round-robin scheduler as baseline
- Priority levels (0–7) in later iteration

### 2.4 System Calls
- `svc #0` entry point
- Function number in `x8`
- First 6 syscalls:

| x8 | Name           | Description               |
|----|----------------|---------------------------|
| 1  | `sys_exit`     | Terminate current process |
| 2  | `sys_yield`    | Voluntarily yield CPU     |
| 3  | `sys_getpid`   | Return current PID        |
| 4  | `sys_write`    | Write to UART (debug)     |
| 5  | `sys_alloc`    | Allocate userland pages   |
| 6  | `sys_free`     | Free userland pages       |

**Exit criteria:**
- [ ] Two concurrent processes run without corrupting each other's memory
- [ ] Timer interrupt fires and switches context
- [ ] `sys_exit` cleanly reclaims process resources
- [ ] All registers correctly saved/restored across context switches

**Estimated effort:** 4–6 weeks

---

## Phase 3 — Drivers and Interrupts

**Goal:** Real hardware drivers with interrupt-driven I/O.

### 3.1 Interrupt Controller (GIC-400)
- Initialize GIC distributor and CPU interface
- Route timer interrupt (PPI) to core 0
- Route UART RX interrupt
- IRQ handler table in C

### 3.2 UART (interrupt-driven)
- Replace polling UART with interrupt-driven RX
- Circular ring buffer for received characters
- Foundation for a keyboard input abstraction

### 3.3 USB HID (keyboard)
- QEMU `virt` exposes USB via EHCI/OHCI
- Basic HID boot protocol (no full USB stack yet)
- Translate HID keycodes to ASCII

### 3.4 Framebuffer Display
- Query framebuffer address and dimensions from DTB
- Linear RGBA framebuffer
- `fb_putpixel(x, y, color)`
- `fb_fillrect(x, y, w, h, color)`
- `fb_blit(dst_x, dst_y, src, w, h)` — copy bitmap region

### 3.5 SD Card / Storage (QEMU virtio-blk)
- virtio-blk driver (simpler than real SD for emulation)
- Read/write 512-byte sectors
- Foundation for filesystem layer

**Exit criteria:**
- [ ] Timer interrupt drives the scheduler (no polling)
- [ ] Keyboard input works in QEMU
- [ ] Solid colored rectangle visible on framebuffer
- [ ] Can read a sector from virtio-blk

**Estimated effort:** 6–8 weeks

---

## Phase 4 — Filesystem and IPC

**Goal:** Persistent storage and inter-process communication.

### 4.1 Virtual Filesystem (VFS)
- Minimal VFS layer with mount points
- File operations: `open`, `close`, `read`, `write`, `seek`, `stat`
- In-memory tmpfs for early userland

### 4.2 FAT32 Driver
- Read-only FAT32 on top of virtio-blk
- Directory traversal, file read
- Write support in second iteration

### 4.3 IPC — Shared Memory and Messages
- Shared memory regions (mapped into two process address spaces)
- Message queue (fixed-size messages, kernel-managed)
- Inspired by KolibriOS's IPC model: simple, no sockets needed initially

**Exit criteria:**
- [ ] Can load and execute a binary from FAT32 image
- [ ] Two processes exchange messages without kernel crash
- [ ] tmpfs supports create/read/write/delete

**Estimated effort:** 4–6 weeks

---

## Phase 5 — GUI Subsystem

**Goal:** A working windowed GUI without X11, Wayland, or any external display server.

### 5.1 Window Manager / Compositor
- Each process owns one or more windows (regions of the framebuffer)
- Z-order stack (front/back)
- Dirty region tracking — only redraw changed areas
- No compositor process: kernel manages the window list directly (KolibriOS model)

### 5.2 2D Rendering Primitives
- All in C, hand-optimized inner loops with NEON intrinsics
- `draw_line`, `draw_rect`, `draw_circle`
- Alpha blending (ARGB8888)
- Clipping to window bounds

### 5.3 Font Rendering
- FreeType 2 (static lib, compiled with `-ffreestanding` patches)
  OR a custom bitmap font renderer for the first iteration
- UTF-8 text rendering
- Basic glyph cache

### 5.4 GUI Event System
- Keyboard and mouse events routed from drivers to focused window
- Simple message-passing: driver → kernel event queue → target process

**Exit criteria:**
- [ ] Two overlapping windows visible, correct z-order
- [ ] Window can be "moved" (redrawn at new position)
- [ ] Text renders with a TTF font
- [ ] Keyboard input reaches the focused window

**Estimated effort:** 8–12 weeks

---

## Phase 6 — Userland and Applications

**Goal:** A usable system with a shell, text editor, and file manager.

### 6.1 Shell
- Minimal command interpreter
- Built-in commands: `ls`, `cd`, `cat`, `run`, `kill`, `mem`
- Runs in a terminal window (GUI)

### 6.2 Text Editor
- Single-file editor (inspired by KolibriOS's Tinypad)
- Keyboard-driven, no mouse required
- Saves to FAT32

### 6.3 File Manager
- List directories
- Copy, move, delete files
- Launch executables

### 6.4 System Monitor
- Running processes, CPU usage, memory usage
- Updates every second via timer

**Exit criteria:**
- [ ] Shell accepts input and runs programs
- [ ] Editor opens, edits, and saves a file
- [ ] System monitor shows live process list

**Estimated effort:** 6–10 weeks

---

## Phase 7 — Networking

**Goal:** TCP/IP stack and basic network applications.

- virtio-net driver (QEMU) / real Ethernet driver (RPi)
- lwIP integration (MIT licensed, designed for embedded)
- DHCP client
- DNS resolver (minimal)
- HTTP client (enough for a simple browser or package fetcher)

**Estimated effort:** 4–8 weeks

---

## Phase 8 — Real Hardware Port (Raspberry Pi 4/5)

**Goal:** Everything from Phase 0–6 running on a physical RPi 4 or 5.

**Delta from QEMU:**
- Replace PL011 UART with BCM UART (mini UART or PL011 at different base)
- Replace virtio-blk with EMMC/SDIO driver
- Replace GIC-400 base address (different on BCM2711)
- Framebuffer via VC4 DRM or direct mailbox interface
- USB via VIA VL805 (RPi 4) — driver needed

**Exit criteria:**
- [ ] Boots from SD card on RPi 4
- [ ] UART output visible on serial adapter
- [ ] Framebuffer shows boot screen on HDMI

**Estimated effort:** 4–6 weeks (assuming clean driver abstraction from Phase 3)

---

## Long-term Vision (post v1.0)

- SMP support (multi-core scheduler)
- Porting to other Cortex-A boards (Orange Pi, Rock Pi 5)
- Dynamic library loading
- Network stack maturity (TLS 1.3 via mbedTLS)
- Audio driver (I2S)
- Package format and installer

---

## Version Targets

| Version | Milestone                              | Phases   |
|---------|----------------------------------------|----------|
| v0.1    | Boots, UART output                     | 0        |
| v0.2    | Memory management working              | 0–1      |
| v0.3    | Preemptive multitasking                | 0–2      |
| v0.5    | Drivers + framebuffer                  | 0–3      |
| v0.7    | Filesystem + GUI basics                | 0–5      |
| v1.0    | Usable on QEMU: shell + editor + net   | 0–7      |
| v1.5    | Running on real RPi 4/5 hardware       | 0–8      |
