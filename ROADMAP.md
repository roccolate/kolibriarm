# Roadmap

This document describes the planned development trajectory for KolibriARM. Each phase has clear entry/exit criteria — a phase is "done" only when all its criteria are met and verified by the listed host tests and QEMU smoke checks.

---

## Phase 0 — Foundation [done]

**Goal:** A binary that boots, prints to UART, and halts cleanly.

**Scope:**
- AArch64 bootloader in pure ASM (`boot/start.S`)
- MMU left disabled (identity mapping)
- BSS zeroing
- Stack initialization
- Jump to `kernel_main()` in C
- UART PL011 driver (polling, no interrupts)
- Linker script placing code at `0x40080000`
- Makefile with `make`, `make qemu`, `make qemu-debug` targets

**Exit criteria:**
- [x] `make qemu` prints a boot message to the terminal
- [x] GDB can attach, set a breakpoint at `kernel_main`, and step through C code
- [x] Binary size under 32 KB

**Progress:**
- Boot sequence (EL1) and basic UART output — implemented
- DTB parsing and RAM map detection — implemented
- Physical memory manager (bitmap) — implemented
- Kernel heap (`kheap`) and `kmalloc`/`kfree` smoke tests — implemented
- VMM helpers, identity page tables and MMU enable — implemented (identity mapping)
- GIC init, timer and basic scheduler with kernel threads demo — implemented

**Estimated effort:** 1–2 weeks

---

## Phase 1 — Memory Management [done]

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
- `vmm_map(vaddr, paddr, flags)` — map a virtual page to a physical frame
- User-accessible mappings via `VMM_FLAG_USER`
- `TTBR0_EL1` install/read helpers for process address spaces
- `vmm_unmap_page()` / `vmm_unmap_range()`

### 1.3 Kernel Heap
- Simple slab allocator or bump allocator
- `kmalloc(size)` / `kfree(ptr)`
- Used internally by the kernel only (no userland malloc yet)

**Exit criteria:**
- [x] MMU enabled, kernel running with identity-mapped virtual addresses
- [x] `pmm_alloc_page()` returns unique non-overlapping frames
- [x] `kmalloc(64)` works without crashing
- [x] Host PMM/kheap tests pass
- [x] Host VMM map/translate/unmap/user-flag tests pass
- [x] QEMU smoke test reaches MMU, timer, and scheduler demo
- [x] User-accessible page mappings exist
- [x] `TTBR0_EL1` helpers exist for Phase 2 process switching
- [x] `vmm_unmap_*()` exists and is tested

**Estimated effort:** 3–5 weeks

---

## Phase 2 — Processes and Scheduler [EL0 demo done]

**Goal:** Multiple EL0 processes can run concurrently under scheduler control. This phase proves the EL0 entry path, syscalls, timer preemption, and PCB save/restore. Fully separate process page tables are tracked explicitly in Phase 2.5.

**Result:** the embedded EL0 demo now creates multiple process table entries with separate stacks. Timer IRQ can preempt one EL0 process, save its frame, load the next READY process, and return to EL0. `sys_yield` and `sys_exit` also switch to the next READY process when possible.

### 2.0 First EL0 Hello World
- Embedded first user program in the kernel image
- Created a user stack and initial EL0 entry path
- Entered EL0 with `eret`
- Handled `svc #0` from EL0
- Dispatched syscall number from `x8`
- Implemented `sys_write` by forwarding stdout/stderr to UART
- Implemented `sys_exit` by returning control to the kernel

**Exit criteria:**
- [x] QEMU prints `Hello from EL0`
- [x] `sys_write` validates the user buffer range before reading it
- [x] `sys_exit` cleanly stops the user task
- [x] Kernel remains alive after the user task exits

### 2.0.1 Cleanup Before Real Processes
- [x] Move `kernel_main()` PMM/kheap/VMM smoke tests behind debug/demo helpers
- [x] Keep the EL0 demo runnable without mixing it into unrelated boot diagnostics
- [x] Replace the fixed embedded-user range check with process-owned user memory metadata
- [x] Keep the next user image linked into the kernel until a tiny loader-owned blob exists
- [x] Add `user_image_t` metadata and a tiny loader helper that prepares a PCB from image base/size/entry plus a user stack
- [x] Copy the linked EL0 blob into per-process loader-owned executable slots before running it
- [x] Move the EL0 demo payload into `programs/` so kernel entry glue and user code are separate modules
- [x] Add a tiny flat image header with magic, image size, and multiple entry offsets
- [x] Build the EL0 demo as a standalone flat binary and embed that serialized image as the current boot-time source

### 2.1 Process Control Block (PCB)
```c
typedef struct process {
    uint32_t pid;
    uint64_t regs[31];       // x0-x30
    uint64_t sp, pc, pstate;
    uint64_t *page_table;    // PGD address
    process_state_t state;   // READY, RUNNING, BLOCKED, ZOMBIE
    struct process *next;
} process_t;
```

Implemented so far:
- [x] Initial PCB structure with saved-register slots, EL0 entry state, page-table pointer, state, exit code, and linked-list pointer
- [x] Syscall dispatch saves the latest EL0 register frame, PC, and PSTATE into the current PCB
- [x] Current-process pointer for syscall dispatch
- [x] Fixed-size process table with allocate/release helpers
- [x] Round-robin READY process selection and ZOMBIE PCB reclamation helpers
- [x] User memory region metadata for validating syscall buffers
- [x] Embedded EL0 demos are allocated from the process table and reclaimed after returning to EL1
- [x] Multiple EL0 demo processes can run in one kernel boot path

### 2.2 Context Switch (ASM)
- Save/restore all AArch64 registers
- Switch page tables via `TTBR0_EL1`
- Implemented in ASM for correctness and speed

Implemented so far:
- [x] Lower-EL synchronous syscall frames save/restore full x0-x30 plus ELR/SPSR
- [x] IRQ entry now preserves ELR/SPSR and exposes an `exception_frame_t` to C
- [x] Timer IRQ path can save the interrupted current process frame into its PCB
- [x] EL1-to-EL0 entry loads PSTATE from the PCB instead of hardcoding it

### 2.3 Preemptive Scheduler
- ARM Generic Timer (CNTV_EL0) as tick source
- Round-robin scheduler as baseline
- Priority levels (0–7) in later iteration

Implemented so far:
- [x] `sys_yield` can cooperatively switch from the current EL0 process to the next READY process
- [x] `sys_exit` switches to the next READY process before returning to EL1 when possible
- [x] Timer IRQ can preempt an EL0 process and switch to the next READY process

### 2.4 System Calls
- `svc #0` entry point
- Function number in `x8`
- Implemented so far:

| x8 | Name           | Description               |
|----|----------------|---------------------------|
| 1  | `sys_exit`     | Terminate current process |
| 2  | `sys_yield`    | Voluntarily yield CPU     |
| 3  | `sys_getpid`   | Return current PID        |
| 20 | `sys_mmap`     | Map anonymous user pages |
| 21 | `sys_munmap`   | Unmap owned anonymous user pages |
| 43 | `sys_write`    | Write to UART-backed stdout/stderr |

`sys_mmap` / `sys_munmap` now create and remove PTE-backed anonymous mappings for the current process. Image and stack regions still use process-owned metadata for syscall pointer validation.

**Exit criteria:**
- [x] Multiple EL0 processes run without corrupting each other's memory
- [x] Timer interrupt fires and switches context
- [x] `sys_exit` cleanly reclaims process resources
- [x] All registers correctly saved/restored across context switches

**Estimated effort:** 4–6 weeks

---

## Phase 2.5 — Real Process Address Spaces [done]

**Goal:** Move from EL0 demos running in the kernel identity map to real per-process user address spaces.

### 2.5.1 Per-process Page Tables
- Create each process page table from a minimal kernel/device mapping template
- Map the user image into a process-owned virtual address range with read/execute user permissions
- Map each user stack into a process-owned virtual address range with read/write user permissions
- Keep kernel-only mappings inaccessible from EL0

### 2.5.2 User Memory Syscalls
- Make `sys_mmap` allocate physical pages and install user PTEs
- Make `sys_munmap` tear down exact user mappings and release backing pages
- Keep process-owned user-region metadata as the validation source for syscalls

### 2.5.3 Loader Integration
- Keep the flat user-image format from Phase 2.0.1
- Load a flat image into process-owned mappings instead of directly executable kernel identity slots
- Switch `TTBR0_EL1` on context switches using each PCB's `page_table`

Implemented so far:
- [x] EL0 demo processes now get distinct `TTBR0_EL1` page-table roots
- [x] Each demo table maps RAM/MMIO for EL1 and remaps that process's image RX user and stack RW user
- [x] Syscall and IRQ process switches install the next process's `page_table` before returning to EL0
- [x] The kernel page table is restored after the EL0 demo returns to EL1
- [x] Host VMM test covers replacing a kernel-only page with a user executable mapping
- [x] `sys_mmap` now allocates physical pages and installs user PTEs in the current process table
- [x] `sys_munmap` tears down exact owned anonymous mappings and frees backing pages
- [x] The EL0 demo writes and reads a byte through `sys_mmap` memory before unmapping it
- [x] `user_vm` helper centralizes anonymous user mapping logic outside syscall dispatch
- [x] EL0 demo images and stacks now use user VAs mapped to loader-owned physical slots
- [x] Kernel page table no longer exposes demo image/stack slots with user permissions
- [x] Host user-vm test verifies physical-backed mappings stay per-process
- [x] Lower-EL memory faults are converted into process exit without crashing the kernel
- [x] Faulting EL0 demo intentionally touches an unmapped pointer and the scheduler continues

**Exit criteria:**
- [x] Multiple EL0 processes have distinct user image and stack mappings
- [x] User writes cannot modify another process's stack or image
- [x] `sys_mmap` / `sys_munmap` create and remove actual PTEs
- [x] Invalid user pointers are rejected or faulted without corrupting kernel state
- [x] Host VMM/process/user-vm tests cover the new mapping behavior
- [x] QEMU smoke test still runs the EL0 demos with timer preemption and a controlled user fault

**Estimated effort:** 2–4 weeks

---

## Phase 3 — Drivers and Interrupts [core criteria met]

**Goal:** Real hardware drivers with interrupt-driven I/O.

### 3.1 Interrupt Controller (GIC-400)
- Initialize GIC distributor and CPU interface
- Route timer interrupt (PPI) to core 0
- Route UART RX interrupt
- IRQ handler table in C

Implemented so far:
- [x] GICv2 distributor and CPU interface initialize on QEMU `virt`
- [x] QEMU `virt` board layer owns GIC base addresses and MMIO mapping
- [x] Timer PPI is enabled through GIC
- [x] IRQ handler table in C with register/unregister helpers
- [x] Timer IRQ is dispatched through the handler table
- [x] UART RX IRQ is registered through the same handler table

### 3.2 UART (interrupt-driven)
- Replace polling UART with interrupt-driven RX
- Circular ring buffer for received characters
- Foundation for a keyboard input abstraction

Implemented so far:
- [x] QEMU `virt` board layer owns PL011 base address and UART0 IRQ number
- [x] PL011 RX/timeout interrupt handler
- [x] Circular RX ring buffer
- [x] Non-blocking `uart_getc_nonblock()` and `uart_rx_available()`
- [x] UART0 RX interrupt enabled on QEMU `virt`
- [x] QEMU `-nographic` console input is echoed by a kernel console thread

### 3.3 Early Input Path
- Use UART console input as the temporary keyboard path in QEMU
- Keep USB HID out of the Phase 3 exit criteria until GUI/user input needs it
- Translate real keyboard input to ASCII when a USB HID path is added later

Implemented so far:
- [x] Early keyboard path via UART console input in QEMU

Deferred:
- [ ] USB HID keyboard driver

### 3.4 Framebuffer Display
- Query framebuffer address and dimensions from DTB
- Linear RGBA framebuffer
- `fb_putpixel(x, y, color)`
- `fb_fillrect(x, y, w, h, color)`
- `fb_blit(dst_x, dst_y, src, w, h)` — copy bitmap region

Implemented so far:
- [x] Linear framebuffer descriptor
- [x] `fb_putpixel()`
- [x] `fb_fillrect()` with clipping
- [x] `fb_blit()` with clipping
- [x] Host tests for framebuffer primitives
- [x] `simple-framebuffer` DTB parser with host tests
- [x] Minimal `virtio-gpu` modern MMIO control queue
- [x] 640x480 framebuffer test pattern flushed to scanout
- [x] Headless QEMU `screendump` verified nonblank rectangle colors

Note: QEMU does not expose a `simple-framebuffer` node by default, and local `ramfb` is missing its option ROM. The visible smoke path currently uses `virtio-gpu-device` through `make qemu-fb`; the `simple-framebuffer` parser remains host-tested for future firmware-provided framebuffers.

### 3.5 SD Card / Storage (QEMU virtio-blk)
- virtio-blk driver (simpler than real SD for emulation)
- Read/write 512-byte sectors
- Foundation for filesystem layer

Implemented so far:
- [x] QEMU `virt` board layer maps the virtio-mmio transport range
- [x] `virtio-blk` MMIO scan finds the block transport exposed by QEMU
- [x] `virtio-blk` probe validates magic/version/device ID
- [x] `virtio-blk` capacity read from config space
- [x] `virtio-blk` modern MMIO queue initialization
- [x] Synchronous sector read path for sector 0 smoke testing
- [x] Host tests for present and non-block-device probe paths
- [x] `make qemu-blk` target creates a tiny raw disk image and attaches it with modern virtio-mmio

**Exit criteria:**
- [x] Timer interrupt drives the scheduler (no polling)
- [x] Keyboard input works in QEMU through the UART console path
- [x] Solid colored rectangle visible on framebuffer
- [x] Can read a sector from virtio-blk

**Estimated effort:** 6–8 weeks

---

## Phase 3.6 — Board Abstraction Cleanup [done]

**Goal:** Keep QEMU moving fast while making the next ARM64 board port a contained driver/platform change.

**Scope:**
- Add a generic `drivers/board.h` interface used by kernel code
- Make `BOARD ?= qemu_virt` selectable in the Makefile
- Remove direct `boards/qemu_virt/board.h` includes from `kernel/`
- Keep MMIO ranges, IRQ numbers, UART setup, and virtio discovery behind board helpers
- Keep QEMU-specific devices useful, but avoid designing generic APIs around QEMU-only details

Implemented so far:
- [x] Added a generic `drivers/board.h` board contract
- [x] Makefile selects `drivers/boards/$(BOARD)/board.o` with `BOARD ?= qemu_virt`
- [x] Kernel init code uses the generic board interface instead of QEMU headers
- [x] IRQ ack/end/spurious handling is routed through board helpers
- [x] QEMU constants stay in `drivers/boards/qemu_virt/`

**Exit criteria:**
- [x] `make BOARD=qemu_virt` builds the current system
- [x] Kernel code includes only the generic board interface
- [x] QEMU virt still boots, receives UART input, draws the framebuffer pattern, and probes virtio-blk
- [x] `PORTING.md` matches the real board interface

**Estimated effort:** 1–2 weeks

---

## Phase 4 — Filesystem and IPC

**Goal:** Persistent storage and inter-process communication.

### 4.0 Boot Program Store / tmpfs Seed
- Register embedded flat user images by name
- Expose a tiny read-only bootfs or tmpfs seed for loader tests
- Spawn a program by name before FAT32 exists
- Keep FAT32 as the next storage-backed source for the same loader path

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
- [ ] Can spawn a named flat user image through bootfs/tmpfs
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
- Built-in bitmap font renderer for the first iteration
- ASCII text rendering first, then UTF-8 once the terminal path is stable
- Optional later: FreeType 2 as a static freestanding library
- Basic glyph cache after the bitmap text path is reliable

### 5.4 GUI Event System
- Keyboard and mouse events routed from drivers to focused window
- Simple message-passing: driver → kernel event queue → target process
- USB HID keyboard/mouse drivers can land here if UART input is no longer enough

**Exit criteria:**
- [ ] Two overlapping windows visible, correct z-order
- [ ] Window can be "moved" (redrawn at new position)
- [ ] Text renders with a built-in bitmap font
- [ ] Keyboard input reaches the focused window

**Estimated effort:** 8–12 weeks

---

## Phase 6 — Userland and Applications

**Goal:** A usable system with a shell, text editor, and file manager.

### 6.0 Developer Kernel Console
- UART-backed monitor for debug commands while the GUI shell is not ready
- Built-in commands such as `help`, `mem`, `ps`, `ticks`, `storage`, and `fb`
- Not a POSIX shell and not the final user-facing terminal

### 6.1 User Shell
- Minimal command interpreter
- Built-in commands: `ls`, `cd`, `cat`, `run`, `kill`, `mem`
- Runs in a terminal window once VFS, loader, and GUI text output exist

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

**Estimated effort:** 4–6 weeks (assuming clean driver abstraction from Phase 3.6)

---

## Phase 9+ — Engine and Multimedia Track

**Goal:** Build a compact multimedia stack on top of the filesystem, GUI,
input, display, and audio foundations without changing the current kernel
milestones. The detailed design lives in [ENGINE_MULTIMEDIA.md](ENGINE_MULTIMEDIA.md).

The external engine proposal is accepted with adjustments:
- Its phase numbers become post-v1.5 phases after the current Phase 8 hardware
  work.
- QEMU remains the primary implementation path first: `virtio-gpu`,
  `virtio-input`, and later `virtio-sound`.
- Raspberry Pi display/audio/input support stays behind board-specific drivers.
- Lua or another script runtime must not live in the kernel. It can be
  considered later as a userland/runtime library.
- Assets move to VFS/resource-manager loading after Phase 4. Embedded bootstrap
  assets remain acceptable until the loader and VFS path exists.
- The first versions are scalar C with tests; NEON paths come only after the
  behavior is stable and measured.

Planned phases:
- Phase 9: display backbone with front/back buffers, present pacing, and logical
  resolution support.
- Phase 10: clipped 2D primitives, blits, and bootstrap bitmap text.
- Phase 11: unified input event queue for UART, virtio-input, and later USB HID.
- Phase 12: PCM audio mixer and board-specific output drivers.
- Phase 13: VFS-backed resource manager for sprites, tilemaps, fonts, and audio.
- Phase 14: compositor refinement with dirty rectangles, cursor, and layers.
- Phase 15: fixed-timestep interactive runtime, collision helpers, entity pools,
  sequencer, and optional userland scripting.

**Exit criteria:**
- [ ] A demo app loads assets through VFS/resource handles
- [ ] A moving sprite or tilemap scene renders through the compositor
- [ ] Keyboard/gamepad-style input reaches the demo through the input layer
- [ ] Audio can mix at least one looping track plus one sound effect
- [ ] The demo reports frame timing and stays within the target frame budget on QEMU

---

## Long-term Vision

- SMP support (multi-core scheduler)
- Porting to other Cortex-A boards (Orange Pi, Rock Pi 5)
- Dynamic library loading
- Network stack maturity (TLS 1.3 via mbedTLS)
- Advanced audio codecs and hardware acceleration
- Package format and installer

---

## Version Targets

| Version | Milestone                              | Phases   |
|---------|----------------------------------------|----------|
| v0.1    | Boots, UART output                     | 0        |
| v0.2    | Memory management working              | 0–1      |
| v0.3    | Preemptive multitasking                | 0–2      |
| v0.4    | Real process address spaces            | 0–2.5    |
| v0.5    | Drivers + framebuffer                  | 0–3      |
| v0.6    | Board abstraction cleanup              | 0–3.6    |
| v0.7    | Filesystem + GUI basics                | 0–5      |
| v1.0    | Usable on QEMU: shell + editor + net   | 0–7      |
| v1.5    | Running on real RPi 4/5 hardware       | 0–8      |
| v2.0    | Engine and multimedia runtime          | 9–15     |
