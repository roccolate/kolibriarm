# Roadmap

Honest trajectory for KolibriARM. The previous roadmap marked a lot of phases
as "done" while the actual system was a foundations layer plus a single EL0
demo blob. This version reflects what is **really** running, what still needs
work, and where the next effort goes.

A phase is "done" only when its exit criteria pass on real QEMU runs (and,
where applicable, on real hardware).

---

## Where we actually are

The current system can boot on QEMU `virt` and reach a serial `k>` prompt, and
then drop into an EL0 demo `u>` prompt. What runs there is one flat image
(`programs/user_demo.S`, 1.4k lines of AArch64 asm) that exports eight entry
points — hello, hello-second, fault, shell, run-hello, run-loop, editor, and
monitor — and treats them as "applications" through `sys_spawn`. There is no
desktop, no mouse cursor, no per-process window ownership, and no real
separation between apps. The framebuffer shows two hardcoded rectangles with
the strings "KOLIBRI ARM" and "FAT32 IPC".

That is a real foundations layer. It is not yet a usable OS and it is not close
to KolibriOS. The work below is what changes that.

### Foundations that are actually in place

- AArch64 boot, BSS clear, EL1 entry, identity-mapped MMU
- PMM (bitmap), kheap (kmalloc/kfree), VMM helpers, `TTBR0_EL1` install/read
- Per-process user address spaces with PTE-backed anonymous mappings
- Preemptive round-robin scheduler with timer IRQ, `sys_yield`, `sys_exit`
- EL0 entry/exit through the exception vector, full register save/restore
- GICv2 init, timer PPI, UART0 RX IRQ, C handler table
- virtio-gpu modern MMIO driver; 640x480 scanout
- virtio-input modern MMIO driver; events queued for keyboard and mouse
- virtio-blk sector read/write; FAT32 BPB parse, root 8.3 listing, limited
  overwrite of preallocated files
- tmpfs (in-RAM) with create/read/write/delete
- Fixed VFS, bootfs seed, named program registry
- Fixed-message IPC `sys_ipc_send` / `sys_ipc_recv`
- Bitmap 5x7 font, scalar 2D primitives, alpha blending, clipping
- Kernel debug console (`k>`) with help/mem/ps/ticks/storage/fb
- LWIP-backed virtio-net + DHCP client, polled by the kernel console thread

### Things that look "done" in the code but are not real

- The "GUI" is two static windows drawn once on boot. No mouse cursor, no
  click handling, no focus visualization, no redraw on event, no per-process
  ownership.
- The "userland" is one .S file with eight labels. `sys_spawn` only changes the
  PC into one of those labels. They share the same page table, the same flat
  memory, the same stack pool, and the same `user_image` blob.
- The "shell" matches full command strings (`ls /fat`, `cat /tmp/note`); it
  does not parse arguments and it cannot list or close windows.
- The "editor" is line-based, no scrolling, no window, and saves into a single
  preallocated file in the FAT32 root.
- Phase 8 (RPi 4 port) builds but has never been booted on hardware.

---

## Next milestone: a real desktop

**Goal:** the kernel boots into a graphical desktop with a visible mouse
cursor, a taskbar, and at least four real EL0 applications — shell, editor,
monitor, and a clock — each running as its own process with its own window
that the user can move, raise, and close. `sys_spawn` launches a real
per-process binary by name, not a label inside one shared blob.

This is the smallest milestone that makes the system feel like KolibriOS
instead of a demo.

### Phase 10.0 — Honest split of the userland

Stop treating `user_demo.S` as the userland. Each "application" becomes its own
flat binary with its own linker script, and the loader learns to find them by
name.

- Refactor `programs/user_demo.S` into one `.S` per app under `programs/apps/`
  (start with `hello`, `loop`, `shell`, `editor`, `monitor`, `fault`).
- Each app defines its own flat image header (magic, size, single entry).
- `boot_program.c` keeps a fixed-size registry of `(name, image, size)` and
  exposes `boot_program_find("shell")` etc.
- VFS exposes each registered image under `/kolibri/<name>` for the existing
  sys_spawn path; FAT32 copies become optional overrides.
- `sys_spawn` only accepts `/kolibri/<name>` and returns the new pid, or
  -errno on failure.

Exit criteria:
- [ ] Six independent flat EL0 binaries built and registered.
- [ ] `sys_spawn("/kolibri/shell", 0)` returns a fresh pid with its own
      page table and stack.
- [ ] Existing host tests still pass.

### Phase 10.1 — Mouse, cursor, hit testing

- Consume the `INPUT_EVENT_MOUSE_MOVE` and `INPUT_EVENT_MOUSE_BUTTON` events
  the virtio-input driver already queues.
- Track cursor `(x, y)` in the kernel and draw a 16x16 cursor on top of the
  desktop redraw.
- Hit-test against the window list and dispatch the focused window on click.
- Reserve a stable syscall number space for window operations (start at 70,
  not 60+, to leave room for the IPC range).

Exit criteria:
- [ ] Cursor visible in `make qemu-fb-visible` and tracks the mouse.
- [ ] `ps` shows the cursor position read from kernel state.
- [ ] Click on a window raises it; click on the desktop deselects.

### Phase 10.2 — Window manager (per-process)

- `gui_window_t` grows an `owner_pid` field; `gui_create_window` requires an
  owner pid and refuses cross-process use.
- `sys_window_create(x, y, w, h, title)` returns a window handle visible to
  the owner only.
- `sys_window_draw_text(handle, x, y, text, color)` for the in-window
  text path apps actually use.
- `sys_window_draw_rect/line/circle` for the same.
- `sys_window_event` blocks the caller until a key/click arrives for one of
  its windows and returns the event.
- `sys_window_close(handle)` removes the window and wakes any blocked owner.
- Kernel redraws the desktop on every state change; apps do not touch the
  framebuffer directly.

Exit criteria:
- [ ] Two apps, two windows, each redraws only its own region on key event.
- [ ] A crashed app's window stays around until the user closes it.
- [ ] A moving drag updates the window in real time at host-testable speed.

### Phase 10.3 — Desktop shell and taskbar

- The kernel boots a small desktop process (the new "panel") that owns the
  taskbar at the bottom.
- The panel reads the list of registered apps from the loader and draws one
  icon per app on the left.
- Clicking an icon calls `sys_spawn` for that app.
- The taskbar shows running apps by pid; clicking an entry raises its window.
- Background is a vertical gradient drawn by the kernel compositor.
- The existing serial `k>` debug console is no longer the primary user
  surface; it stays as a debug fallback.

Exit criteria:
- [ ] Boot reaches the desktop without the user typing anything.
- [ ] Clicking the editor icon spawns the editor app, which opens its own
      window and starts editing `/tmp/note.txt`.
- [ ] Closing the editor's window does not crash the panel or the kernel.

### Phase 10.4 — Real apps (minimum four)

Each app is a small flat binary that owns one window and reacts to events.

- `shell` — parses a line of text, splits args, calls `sys_spawn` or
  `sys_window_list` etc. No more hardcoded full-string matches.
- `editor` — one file per window, arrow keys to move, backspace deletes,
  ctrl-s saves to `/tmp/note.txt`, ctrl-q closes the window.
- `monitor` — redraws the process list and free-page count every second
  using a redraw event the WM sends.
- `clock` — redraws the wall time from the existing timer ticks.

Exit criteria:
- [ ] All four apps run together without crashing.
- [ ] Editing in `editor` is visible only inside the editor window.
- [ ] `shell` correctly spawns any other registered app by name with args.

### Phase 10.5 — Polish and KolibriOS ports

- Replace the 5x7 font with an 8x8 font ported from KolibriOS's `8X8ISXP`
  table; this is the only piece of KolibriOS source we need at first.
- Window decorations: title bar with text, minimize/close boxes drawn by the
  WM, active vs inactive border colors.
- Add the `KOS` flat format as a synonym for our flat image header so we can
  reuse KolibriOS tools for cross-building demos in the long term.
- Sticky key handling for the shell (`up` arrow recalls the previous line).

Exit criteria:
- [ ] Visible title bars with text on every app window.
- [ ] Cursor changes shape over clickable decorations (hand on icon, arrow
      on text).
- [ ] Building from a KolibriOS `.kos` demo file works for at least hello.

---

## After the desktop

The roadmap does not commit to anything beyond Phase 10.5 yet. The next set of
candidates, in rough order of return on effort:

- Real FAT32 write (cluster allocation, file create/delete, rename, LFN).
- USB HID keyboard and mouse drivers, so the QEMU UART input is no longer the
  primary path.
- SMP: enable the secondary cores after the uniprocessor desktop is stable.
- A minimal TCP/HTTP client for `wget` style apps.
- Real hardware boot on RPi 4 with the same desktop visible over HDMI.

The RPi 4 port stays in the repo as a buildable board, but it is **not**
"in progress" until the QEMU desktop ships first.

---

## Style boundaries (carried over from AGENTS.md)

- No libc, no POSIX, no Linux compatibility layer.
- AArch64 asm only at the CPU boundary, with a comment when control flow is
  subtle.
- Reuse existing modules before adding new ones: `kernel/mm`, `kernel/sched`,
  `kernel/timer`, `drivers/irq`, `drivers/uart`, `kernel/gui`.
- Prefer a `drivers/boards/qemu_virt/` platform layer before touching RPi 4.
- Port ideas from KolibriOS, not its x86 asm. The 8x8 font and the
  `KOS` header are the first ports; syscall IDs, IPC semantics, and window
  list layout are next.
- Keep the kernel readable in one sitting. If a module stops fitting on a
  few pages, split it before adding features.

---

## Version targets

| Version | Milestone                                       | Phases     |
|---------|-------------------------------------------------|------------|
| v0.1    | Boots, UART output                              | 0          |
| v0.2    | Memory management working                       | 0–1        |
| v0.3    | Preemptive multitasking                         | 0–2        |
| v0.4    | Real process address spaces                     | 0–2.5      |
| v0.5    | Drivers + framebuffer                           | 0–3        |
| v0.6    | Board abstraction cleanup                       | 0–3.6      |
| v0.7    | Multiple real EL0 apps + per-process windows    | 0–10.0–10.4 |
| v0.8    | QEMU desktop: panel + taskbar + 4 apps + mouse  | 0–10.5     |
| v1.0    | Usable on QEMU: real desktop, real apps         | 0–10.5     |
| v1.5    | Running on real RPi 4 hardware                  | 0–10.5 + RPi bring-up |
| v2.0    | Engine and multimedia runtime (see ENGINE_MULTIMEDIA.md) | 9–15 |
