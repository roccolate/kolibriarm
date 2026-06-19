# Roadmap

Honest trajectory for KolibriARM. The previous roadmap marked a lot of phases
as "done" while the actual system was a foundations layer plus a single EL0
demo blob. This version reflects what is **really** running, what still needs
work, and where the next effort goes.

A phase is "done" only when its exit criteria pass on real QEMU runs (and,
where applicable, on real hardware).

---

## Where we actually are

The current system boots on QEMU `virt`, initializes the kernel foundations,
mounts bootfs/VFS, and starts `/kolibri/panel` as the first EL0 app. The
serial `k>` console still exists as a debug fallback, but the intended primary
surface is now the graphical desktop.

Userland is no longer one `programs/user_demo.S` blob. Each app under
`programs/apps/` builds as a separate flat AArch64 image, is registered in
`kernel/boot_program.c`, and is exposed through bootfs under
`/kolibri/<name>`. `sys_spawn` loads those named images into separate
processes with their own process state, user stack, and page table.

The GUI is an experimental kernel compositor with per-process window
ownership, focus, cursor state, click-to-raise, window dragging, title bars,
and title-bar close events. The desktop starts empty; the panel owns the
taskbar window and launches apps. `shell`, `clock`, `editor`, and `monitor`
are windowed apps now.

This is close to an alpha desktop foundation, but it is not a complete alpha
yet. The remaining blockers are practical: make the interactive QEMU launch
and close checks reliable, decide the first redraw/expose rule that survives
overlapping windows, and keep the app ABI small enough to replace direct
assembly syscalls with a tiny userland library later.

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
- Fixed VFS, bootfs seed, named program registry, `/kolibri/<name>` app paths
- Fixed-message IPC `sys_ipc_send` / `sys_ipc_recv`
- Bitmap 5x7 font, scalar 2D primitives, alpha blending, clipping
- Kernel-owned GUI compositor with per-process windows, cursor, focus, drag,
  title bars, and title-bar close events
- Panel taskbar app booted automatically as the first EL0 process
- Kernel debug console (`k>`) with help/mem/ps/ticks/storage/fb
- From-scratch virtio-net + DHCP client, polled by the kernel console thread

### Remaining rough edges

- The app ABI is still direct AArch64 assembly syscalls; there is no C
  userland helper library yet.
- The shell owns a window and supports a small command set, but it has no real
  argv passing, scrollback, cursor rendering, or terminal emulation.
- The editor owns a window and edits `/tmp/note`, but it is intentionally
  minimal: no cursor rendering, no scrolling, no arrow movement, and only the
  first screenful is drawn.
- The monitor owns a window and draws a compact `sys_proclist`/`sys_meminfo`
  view, but it still has only the simplest fixed layout.
- Window redraw is still whole-desktop and demo-level; there are no per-window
  backing buffers or damage rectangles yet.
- Resize is defined in the event ABI but not produced. Minimize/maximize and
  taskbar-owned focus controls are not implemented.
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

Stop treating `user_demo.S` as the userland. Each application is now its own
flat binary with its own linker script, and the loader can find them by name.

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
- [x] Nine independent flat EL0 binaries built and registered.
- [x] `sys_spawn("/kolibri/shell", 0)` returns a fresh pid with its own
      page table and stack.
- [x] Existing host tests still pass.

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
- [x] `ps` shows the cursor position read from kernel state.
- [x] Click on a window raises it above overlapping windows.
- [x] Click on the desktop deselects the focused window.

### Phase 10.2 — Window manager (per-process)

- `gui_window_t` grows an `owner_pid` field; `gui_create_window` requires an
  owner pid and refuses cross-process use.
- `sys_window_create(x, y, w, h, title)` returns a window handle visible to
  the owner only.
- `sys_window_draw_text(handle, x, y, text, color)` for the in-window
  text path apps actually use.
- `sys_window_draw_rect` for the same. Lines, circles, and bitmaps are still
  future draw primitives.
- `sys_window_event` yields for a bounded number of scheduler turns, then
  returns packed events or `ERR_AGAIN`.
- `sys_window_destroy(handle)` removes an owner window. Title-bar close clicks
  are delivered to the owner as `GUI_EVENT_CLOSE`.
- Kernel redraws the desktop on every state change; apps do not touch the
  framebuffer directly.

Exit criteria:
- [ ] Two apps, two windows, each redraws only its own region on key event.
- [x] A crashed app's window stays around until the user closes it.
- [x] A moving drag updates the window in real time at host-testable speed.

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
- [x] Boot reaches the desktop without the user typing anything.
- [ ] Clicking the editor icon spawns the editor app, which opens its own
      window and starts editing `/tmp/note`.
- [ ] Closing the editor's window does not crash the panel or the kernel.

### Phase 10.4 — Real apps (minimum four)

Each app is a small flat binary that owns one window and reacts to events.

- `shell` — owns one window, parses a small command set, and supports
  `run <name>` for any registered app. It still ignores trailing args and uses
  a fixed line display rather than terminal emulation.
- `editor` — owns one window, appends printable input, supports backspace and
  newline, saves `/tmp/note` with ctrl-s, and closes with ctrl-q or title-bar
  close. Cursor movement and scrolling are not implemented yet.
- `monitor` — owns one window and redraws process, free-page, and tick data
  from the existing process/info syscalls.
- `clock` — owns one window and redraws time from the existing timer ticks.

Exit criteria:
- [ ] All four apps run together without crashing.
- [ ] Editing in `editor` is visible only inside the editor window.
- [x] `shell` can spawn any registered app by name with `run <name>`.
- [ ] `shell` correctly passes args to spawned apps.

### Phase 10.5 — Polish and KolibriOS ports

- Replace the 5x7 font with an 8x8 font ported from KolibriOS's `8X8ISXP`
  table; this is the only piece of KolibriOS source we need at first.
- Window decorations: title bar with text, minimize/close boxes drawn by the
  WM, active vs inactive border colors.
- Add the `KOS` flat format as a synonym for our flat image header so we can
  reuse KolibriOS tools for cross-building demos in the long term.
- Sticky key handling for the shell (`up` arrow recalls the previous line).

Exit criteria:
- [x] Visible title bars with text on every app window that opts in via
      `sys_window_set_title(window_id, title_ptr, title_h)`. `shell`,
      `editor`, `monitor`, and `clock` are current opt-ins. The panel stays a
      bar without title text so its 32 px height can keep all launcher
      buttons.
- [x] Cursor changes shape over kernel title decorations (hand over title
      bar/close box, arrow elsewhere).
- [x] Cursor changes shape over taskbar launcher buttons through
      `sys_cursor_set_shape(0=arrow, 1=hand)`.
- [ ] Building from a KolibriOS `.kos` demo file works for at least hello.

---

## After the desktop

The roadmap does not commit to anything beyond Phase 10.5 yet. The next set of
candidates, in rough order of return on effort:

- **Per-window backing buffers or damage tracking (deferred).** Today's
  compositor repaints the full desktop on every dirty tick, which is
  visually fine for move/drag and for apps that repaint their whole
  content area (shell, clock, editor, and monitor do this now; future windowed
  apps must do the same until the rule changes). It
  becomes fragile only when an app relies on partial updates that
  overlap with another window's redraw between ticks. Implement the
  first concrete case of that breakage before designing the
  abstraction: choose between per-window `uint32_t *backing[w*h]`
  (expensive; one full-window allocation per window from kheap) or
  damage-rectangle tracking (cheap; partial draws can still be
  clobbered by interleaved compositor passes). Until a real partial
  update bug appears, the alpha rule is: apps repaint their entire
  content area on every change.
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
