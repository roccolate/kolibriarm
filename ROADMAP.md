# Roadmap

Honest trajectory for KolibriARM. The first desktop milestone (Phase 10.0–10.5)
is functionally complete on QEMU `virt`: the kernel boots into a graphical
desktop, the panel owns the taskbar, and shell / editor / monitor / clock are
real per-process EL0 apps that own windows, take input, and redraw through the
per-rect compositor path. The host test suite covers the window ABI, IPC,
process isolation, partial redraw, USB HID parsing, and the syscall number
table; `make` and `make -C tests test` both pass.

This file is intentionally shorter than the previous version. It concentrates
on **what is actually missing**, ordered roughly by return on effort. The
"version targets" table at the bottom still records where the milestones
landed.

A phase is "done" only when its exit criteria pass on real QEMU runs (and,
where applicable, on real hardware).

---

## What is in place today

The system boots on QEMU `virt`, brings up virtio-gpu (640×480), virtio-input,
GICv2, virtio-blk/FAT32, virtio-net/DHCP, USB HID enumeration, and the
serial `k>` debug console. The kernel lands on a graphical desktop whose first
userland process is the panel taskbar at the bottom of the screen.

Per-app, per-process windowing is real:

- Each app under `programs/apps/` builds as a flat AArch64 image, is
  registered in `kernel/boot_program.c`, exposed through bootfs under
  `/kolibri/<name>`, and is launched as its own process with its own page
  table and stack.
- `SYS_SPAWN_ARGV` (8) places `argc` in `x0` and `&argv[0]` in `x1` per the
  AArch64 procedure-call ABI. The shell splits `run X Y Z` on whitespace and
  forwards the tokens; the editor prints the received argv.
- Per-window BGRA backing buffer in the kernel. App draws land in the
  backing; the compositor blits during `gui_draw`. Drags and focus changes
  carry content with the window.
- Coalesced damage-rectangle tracking (cap 32 + "full" sentinel). Every
  app pushes the smallest content-local rect that covers its last batch
  of draws through `SYS_WINDOW_FLUSH` (80). The strict tests in
  `tests/test_gui.c` lock down the in-rect fill, off-screen no-op, and
  multiple disjoint rects.
- Kernel-owned GUI compositor with per-process windows, focus, click-to-
  raise, window dragging, title bars with text, title-bar close events,
  16×16 cursor with arrow/hand shapes, vertical gradient background.

The ABI is frozen. `kernel/syscall_numbers.h` lists every implemented syscall
with a `_Static_assert` next to each value; `tests/test_syscall_abi.c`
exercises each dispatch entry at runtime. Any new syscall must add a row in
SYSCALLS.md, an entry in `syscall_numbers.h`, the dispatch case in
`kernel/syscall.c`, and a host test — in the same commit.

The kernel debug console (`k>`) still ships with `help`, `mem`, `ps`,
`ticks`, `storage`, `fb`, `mouse`, and `click` for headless diagnostics.

---

## What is actually missing

Listed roughly by return on effort. Each item names the file or syscall it
would touch.

### 1. Window placement syscalls (move / resize / get-bounds) — done

`sys_window_get_bounds` (81) and `sys_window_set_bounds` (82) land in
the 70..82 window range. `get_bounds` reads `(x, y, w, h)` for the
caller with ownership validation; `set_bounds` moves and/or
resizes the window in one step, validates the new geometry against
the desktop framebuffer, and delegates to `gui_resize_window`
which reallocates the per-window backing buffer on size change and
pushes `GUI_EVENT_RESIZE` onto the owner's event queue. The
`show`/`hide` pair is still unimplemented and stays in the
"planned but not implemented" list.

### 2. Editor cursor and arrow keys — done

The editor now tracks a `caret` (0..file_len), renders a 2×8 block at
`(12 + caret_col*8, 28)`, supports Left/Right within the line and
Up/Down between lines (snapping to the shorter line's end), and
inserts / deletes at the caret instead of always at the buffer's
end. Newlines split lines; the line containing the caret is the
one redrawn. Save still writes the whole buffer from offset 0 (full
rewrite of a small file), which keeps the I/O path the same.

The shell already had `INPUT_KEY_UP/DOWN` from the existing
sticky-key wiring; `INPUT_KEY_LEFT/RIGHT` follow the same convention
and the kernel already produces them through `ESC [ D`/`ESC [ C`
(see `drivers/input/input.c`).

### 3. Shell scrollback and prompt placement — done

The shell now keeps a 256-entry circular log buffer (depth
`LOG_DEPTH=256`) of `LINE_CAP`-byte lines, with a `scroll_offset`
that the user moves via Page Up / Page Down. The prompt row sits
at `28 + DISPLAY_LINES*16` regardless of `scroll_offset`, so it
is always anchored to the bottom of the visible area. Any
printable key, Enter, or Backspace resets `scroll_offset` to 0
so the shell auto-follows new output as soon as the user touches
the keyboard. The kernel input parser learned `ESC [ 5~` and
`ESC [ 6~` (Page Up / Page Down) in the same commit — the new
synthetic keys are `INPUT_KEY_PGUP` (0x105) and `INPUT_KEY_PGDN`
(0x106).

The "running…" marker that the ROADMAP mentions is intentionally
deferred: it needs a `sys_proclist` poll on every redraw to
notice the child exit, which doubles the syscall rate of an
idle shell. The same plumbing will land as a follow-up.

### 4. Resize events — done

`GUI_EVENT_RESIZE` is now produced by `gui_resize_window` (and
therefore by `sys_window_set_bounds`) whenever the new `(w, h)`
differs from the current window size. The event carries
`data1 = new_w` and `data2 = new h`. The host suite's
`test_window_abi_resize_window_updates_geometry_and_queues_event`
verifies the round trip: pop the queue before any move, assert
that a same-size move leaves the queue empty, then resize and
assert the resize triple is sitting at the head.

### 5. Minimize / maximize and taskbar focus controls — done

The kernel-side half (syscalls 83/84/85, the three title-bar
siblings, the per-window minimised flag) landed earlier. The
panel-side half landed with the panel migration to libkarm: the
panel now calls `gui_window_state` for each running-apps slot and
renders minimised windows in a greyed colour (`COL_RUN_MIN`), and
the click handler routes minimised slots to
`gui_window_restore` instead of `SYS_WINDOW_FOCUS`. The
visible-side affordance is now complete: minimise a window via
its title-bar button, the panel slot greys out, click the slot,
the window reappears with the right z-order.

### 6. Per-window cursor region registry — done

`SYS_CURSOR_REGISTER_REGION` (86) installs up to 8 content-local
`(x, y, w, h, shape)` regions per window. The kernel walks the slots
in ascending order on every cursor refresh and uses the first region
whose rect contains the cursor, which means a region can also
*override* the implicit HAND on a title bar. Slots can be cleared
with the `GUI_CURSOR_REGION_DELETE` (0xffffffff) sentinel. Ownership
is enforced: non-owner pids get `ERR_PERM`. The panel replaces its
old `SYS_CURSOR_SET_SHAPE` hover calls with one registration per
launcher button, so the cursor cannot leak a HAND shape into the
desktop above the panel when the cursor leaves the launcher row.
`libkarmdesk/gui.h` adds `gui_cursor_register_region` and the host
suite (`tests/test_gui.c`) covers install / override / first-slot
priority / DELETE / invalid inputs / destroy-clear / constant sanity.

### 7. Interactive QEMU verification (no host test substitutes)

The host suite covers the GUI primitives in isolation, but several exit
criteria only become true when a windowed session runs end to end. The
manual checks the README needs to advertise:

1. `make qemu-fb-visible` — confirm the cursor is visible and tracks the
   virtio mouse.
2. Same target — click the editor icon on the panel, type into the
   editor, and confirm the chars appear only inside the editor window.
3. Same target — open all four apps, close them via title-bar close,
   confirm no crash and no leaked window.
4. `make qemu-usb` — confirm the boot prints "USB: device on port 0/1"
   reach a clean input loop. Full HID delivery needs a MMIO-BAR UHCI,
   which qemu-virt does not expose; record the gap rather than chasing it.

There is no scripted screenshot test yet. Adding one (a `tests/screenshot`
that boots QEMU headless, captures the framebuffer, and diffs against a
checked-in PNG) would catch visual regressions without a human in the loop.

### 8. C userland library — `libkarm` + `libkarmdesk`

Every app is hand-written AArch64 assembly that issues `svc #0` directly,
with its own `_start` and its own copy of the syscall numbers. This does
not scale past a handful of one-screenful demos. AGENTS.md already plans
two libraries; this is the concrete plan to build them.

**`programs/libkarm/`** — process, memory, I/O, IPC, and system-info
syscalls only (numbers 1–61, 100–102). Stable surface, expected to barely
change once written, since `kernel/syscall_numbers.h` already freezes
these numbers with a `_Static_assert` per entry.

- `syscall.S` — the only file in userland allowed to contain `svc #0`.
  Raw numbered trampolines (`__syscall0`..`__syscall6`).
- `syscall.h` — one typed wrapper per syscall (`kli_write`, `kli_exit`,
  `kli_spawn_argv`, …), naming convention `kli_<name without sys_ prefix>`.
- `errno.h` — named constants mirroring the frozen error table, plus
  `kli_isok()` / `kli_again()` helpers. `ERR_AGAIN` is normal control flow
  for UART read, IPC, and window-event polling — wrappers must not treat
  it as a hard failure.
- `crt0.S` — shared `_start`. Accepts `x0=argc, x1=&argv[0]` from
  `SYS_SPAWN_ARGV`, falls back to `argc=0, argv=NULL` when spawned via
  plain `sys_spawn`, calls `int main(int argc, char **argv)`, forwards
  the return value to `sys_exit`.
- `string.c` / `string.h` — freestanding `memcpy`, `memset`, `memmove`,
  `strlen`, `strcmp`, a bounded `strlcpy`, and `kli_utoa`/`kli_itoa` for
  `monitor`/`clock` to render counters. No `printf`.

**`programs/libkarmdesk/`** — window/compositor syscalls only (70–80).
Deliberately separate from `libkarm` because this range is the one still
churning (item #1 above adds `sys_window_get_bounds`/`set_bounds`, item #4
adds the resize event, item #5 adds minimize/maximize). Isolating it means
those additions touch one small library, not the stable syscall surface.

- `gui.h` — thin wrappers for `sys_window_create` through
  `sys_window_flush`, plus the frozen `gui_event_t` triple
  (`type, data1, data2`, event IDs 1–6). Every app already calls
  `SYS_WINDOW_FLUSH` (80) directly today; this just gives that call a
  typed signature instead of an inline `svc #0`.

**Migration is incremental, gated by the existing host suite:**

1. Build both libraries. Nothing in `programs/apps/` changes yet.
2. Port the smallest app (`clock`, not `shell` or `editor`) to
   `crt0.S` + `syscall.h`. Run `make -C tests test` and `make qemu`.
   Hard gate before touching the next app.
3. Migrate `monitor`, then `editor`, then `shell`, same gate each time.
4. Only after every app is on `libkarm`, switch their window calls to
   `libkarmdesk`'s `gui.h`.
5. No syscall renumbering during this work. If a number looks wrong,
   stop and raise it — don't shift the 70–79 GUI range into 60–69,
   that's IPC's range (SYSCALLS.md already warns about this).

Exit criteria:

- [x] `libkarm` and `libkarmdesk` exist and compile standalone.
- [x] `clock` builds and runs against `libkarm`/`crt0.S` in
      `make qemu` with no user-visible behavior change.
- [x] All four apps migrated, each in its own commit.
- [x] `tests/test_syscall_abi.c` and the full host suite still pass.
- [x] No entries in `kernel/syscall_numbers.h` changed value.

### 9. RPi 4 hardware bring-up

The RPi 4 build target is wired up but has never been booted on hardware.
The qemu-virt desktop ships first; the RPi port waits until the kernel
contract is stable enough that bringing it up does not require touching
the core.

### 10. Resilience: what happens when the panel crashes — done

`panel_boot_run_with_recovery` (in `kernel/panel_boot_recovery.c`) wraps
`panel_boot_run` and relaunches the panel up to
`PANEL_BOOT_RECOVERY_MAX_ATTEMPTS` (3) times after it exits, whether
the exit was a clean `sys_exit` or a fault that funnelled through the
same return trampoline. `kernel.c` calls the recovery variant from
`run_panel_boot_smoke`; on the final failed attempt the wrapper logs
"panel_boot: giving up after N attempts" before returning so the
desktop is not silent. The policy decision is in the pure helper
`panel_boot_recovery_decide`, which is covered by
`tests/test_panel_boot_recovery.c` (constant sanity, every branch of
the policy, plus integration tests that verify the wrapper invokes
`panel_boot_run` exactly `MAX_ATTEMPTS` times and propagates the
final exit code). A clean shutdown syscall would slot in here as an
extra branch in `decide`.

### 11. Engine and multimedia runtime (deferred)

`docs/ENGINE_MULTIMEDIA.md` sketches a separate runtime for graphics and
audio. That work is intentionally deferred until after the desktop
milestone is solid.

---

## After the desktop

The next set of candidates beyond this list, in rough order of return on
effort:

- **Per-window backing buffers (done).** Every owner-drawn window owns a
  per-window BGRA buffer; drags and focus changes carry content with the
  window.
- **USB HID foundations (done).** `drivers/pci/` walks the ECAM; the
  boot-protocol translator feeds the same `input_queue` the UART and
  virtio-input use; the kernel reaches "USB: device on port 0/1" on the
  qemu-xhci path. Full HID delivery needs a UHCI with MMIO BARs, which
  qemu-virt does not expose.
- **Switch apps to per-rect redraws (done).** Editor, clock, monitor,
  shell, and the panel each push the smallest content-local rect that
  covers their last batch of draws through `SYS_WINDOW_FLUSH` (80).
- **Remove the embedded user demo (done).** `kernel/user_demo.{c,h}` was
  renamed to `kernel/panel_boot.{c,h}`. `kernel_main` boots the panel
  process registered through bootfs.

Beyond those:

- A scripted screenshot test for the desktop milestone.
- `libkarm` (syscall wrappers) and `libkarmdesk` (window/compositor
  helpers) so future apps can be written in C.
- TCP/HTTP for `wget`-style apps.
- SMP: enable the secondary cores after the uniprocessor desktop is
  stable.
- Real hardware boot on RPi 4 with the same desktop visible over HDMI.

---

## Style boundaries (carried over from AGENTS.md)

- No libc, no POSIX, no Linux compatibility layer.
- AArch64 asm only at the CPU boundary, with a comment when control flow
  is subtle.
- Reuse existing modules before adding new ones: `kernel/mm`,
  `kernel/sched`, `kernel/timer`, `drivers/irq`, `drivers/uart`,
  `kernel/gui`.
- Prefer a `drivers/boards/qemu_virt/` platform layer before touching
  RPi 4.
- Port ideas from KolibriOS, not its x86 asm. The 8×8 font and the `KOS`
  header are the first ports; syscall IDs, IPC semantics, and window list
  layout are next.
- Keep the kernel readable in one sitting. If a module stops fitting on
  a few pages, split it before adding features.

---

## Version targets

| Version | Milestone                                       | Phases                |
|---------|-------------------------------------------------|-----------------------|
| v0.1    | Boots, UART output                              | 0                     |
| v0.2    | Memory management working                       | 0–1                   |
| v0.3    | Preemptive multitasking                         | 0–2                   |
| v0.4    | Real process address spaces                     | 0–2.5                 |
| v0.5    | Drivers + framebuffer                           | 0–3                   |
| v0.6    | Board abstraction cleanup                       | 0–3.6                 |
| v0.7    | Multiple real EL0 apps + per-process windows    | 0–10.0–10.4           |
| v0.8    | QEMU desktop: panel + taskbar + 4 apps + mouse  | 0–10.5                |
| v1.0    | Usable on QEMU: real desktop, real apps         | 0–10.5                |
| v1.5    | Running on real RPi 4 hardware                  | 0–10.5 + RPi bring-up |
| v2.0    | Engine and multimedia runtime                   | 9–15                  |
