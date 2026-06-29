# Roadmap

ArmoniOS is at the **v0.9 QEMU desktop baseline**. The kernel boots on QEMU
`virt`, starts a graphical desktop, runs separate EL0 apps, and keeps the
debug console available for headless work.

This roadmap is the live coordination plan. Completed debt history lives in
`docs/TECH_DEBT_REVIEW.md`; detailed architecture notes live in
`docs/CURRENT_STATE.md`.

Latest verified size: `kernel.bin: 92696 bytes (limit: 100000)`.

---

## Current Baseline

What is already in place:

- QEMU `virt` boot with AArch64 EL1 kernel and EL0 user processes.
- Per-process page tables, user image/stack regions, and anonymous user mmap.
- KLI1 flat app images exposed through `/armonios/<name>`.
- Kernel GUI compositor with owned windows, focus, drag, title bars, close,
  minimize/restore, cursor regions, backing buffers, and damage rectangles.
- Desktop apps: `panel`, `shell`, `editor`, `files`, `monitor`, and `clock`.
- VFS with bootfs, tmpfs, FAT32 integration, dynamic FAT32 root-file opens,
  FAT32 `O_CREAT` for `/fat/<8.3-name>`, and a QEMU FAT32 smoke test.
- virtio-gpu, virtio-input, virtio-blk, virtio-net/DHCP, and xHCI USB HID
  paths for QEMU.
- Host tests for memory, process isolation, scheduler behavior, syscalls,
  window ABI, FAT32, DHCP options, USB/HID parsing, GUI, and app image layout.
- Userland app stack usage measured by `make stack-check`; current maximum is
  368 bytes.

What v0.9 does not claim:

- Real Raspberry Pi hardware boot.
- USB hub support.
- TCP/HTTP applications.
- Automated screenshot diffing.
- SMP.
- Audio, multimedia, or game/runtime APIs.

---

## Release Gates

Use these for kernel, driver, syscall, boot, ABI, Makefile, and userland
changes that affect shipped app images:

```sh
make
make size
make -C tests test
make stack-check
make qemu-fs-test
timeout 25s make qemu-fb
timeout 25s make qemu-usb
timeout 25s make qemu-net
```

Before a release tag, also run one visible desktop pass:

```sh
make qemu-fb-visible
```

Manual visible-pass checklist:

- panel appears and shows launchers for shell, editor, files, monitor, and
  clock;
- each launcher starts the expected app when it is not running, and
  focuses/restores the existing window when it is already running;
- windows can be raised, dragged, closed, minimized, and restored;
- editor accepts typing and basic navigation;
- files lists `/fat`, creates FAT32 root files, opens the selected file in
  editor, renames files, and deletes files after confirmation;
- shell commands `help`, `ls`, `ps`, `ticks`, `mem`, `run editor`,
  `run files`, `run monitor`, `run clock`, `ls /fat`, `kill last`, and `exit`
  behave as expected;
- no unexpected user fault, scheduler stall, or compositor blank frame.

---

## v1.0: Stable QEMU Desktop

Goal: make the current QEMU desktop boringly repeatable. v1.0 is not the
Raspberry Pi release; it is the stable QEMU release.

Allowed work:

- Fix regressions found by the release gates.
- Tighten logging where a runtime failure is ambiguous.
- Keep docs, syscall tables, and app image-size tests in sync.
- Touch `kernel/gui_*`, `drivers/usb/xhci.c`, or networking only when a gate
  exposes a concrete issue.

Avoid for v1.0:

- new large subsystems;
- new syscalls unless a release blocker cannot be solved without one;
- broad GUI rewrites;
- speculative USB or network refactors;
- Raspberry Pi hardware work.

Exit criteria:

- every release gate passes from a clean tree;
- one `make qemu-fb-visible` pass is manually checked;
- README, `docs/CURRENT_STATE.md`, `docs/SYSCALLS.md`, and this roadmap match
  the shipped behavior;
- the kernel remains under `KERNEL_SIZE_LIMIT`;
- optional: tag `v1.0` only after the user explicitly asks for the tag.

---

## Desktop-Core Usability Track

Goal: make the shipped desktop apps useful without destabilizing the kernel.
Do not choose the final version label for this track until the behavior passes
the gates below; it can land as v1.0, v1.1, or another milestone depending on
release timing.

Current scope:

- Shell remains a GUI user shell, not a Menuet/Kolibri-level terminal.
- Editor remains a compact text editor with a 512-byte buffer and one-line
  rendering until manual use proves that blocks the core flow.
- Panel keeps fixed launchers for the shipped apps, does not allow nested panel
  launches, and focuses/restores an already running app instead of spawning a
  duplicate from the launcher row.
- `files` is a core shipped app for FAT32 root-file management.
- `SYS_OPEN` carries the small filesystem ABI expansion: access mode bits plus
  `O_CREAT = 0x40`. No new syscall number is used.

Implemented first pass:

- Shell command text is clearer for `run`, `kill last`, `ls`, `mem`, `ps`, and
  `ticks`; `pwd`, `cd`, cwd-relative `ls`, and `cat` are implemented with the
  same fixed-buffer parser.
- Editor shows the active path and save/open status in the window, and uses
  `O_RDWR | O_CREAT` for editable `/fat/<name>` paths.
- Panel launcher row is `shell`, `editor`, `files`, `monitor`, `clock`, with
  duplicate launcher clicks treated as focus/restore.
- `files` lists `/fat`, supports Up/Down selection, Enter-to-open in editor,
  create-name mode, rename mode, delete confirmation, and Ctrl-Q/close exit.
- FAT32 root names stay 8.3-only for now and are rejected in userland before
  hitting the kernel.

Still required before calling the track done:

- Run the full release gates from this file.
- Run a visible desktop pass that creates, edits, saves, renames, deletes, and
  lists a FAT file.
- Decide whether stale dynamically mounted FAT VFS nodes after rename/delete
  need cleanup before release, or whether `/fat` directory reads are enough for
  the current workflow.
- Keep app image-size tests and `make stack-check` green.

Shared userland rules:

- Keep all app syscall calls behind `libkarm` / `libkarmdesk` wrappers.
- Keep app persistent state out of the fixed 4 KB stack.
- Add small app-level helpers only when they reduce image size or clarify ABI
  usage.
- Reuse `programs/libkarm` only when linker garbage collection keeps the
  resulting app image small.

Exit criteria:

- `make stack-check` remains comfortably under the limit;
- app image sizes are pinned by tests;
- visible QEMU desktop pass covers every shipped app;
- shell/editor/panel behavior is documented in README or `docs/CURRENT_STATE.md`
  if it changes visible user workflows;
- no kernel API churn unless the userland need is proven.

## Minimal Engine Track

Start only after the desktop-core gates pass. The first engine work stays in
userland:

- a tiny helper layer over the existing `libkarmdesk` draw/event calls, or a
  small demo app if that proves the API shape more clearly;
- no new kernel graphics, audio, or input syscalls until a demo proves the
  missing capability;
- no audio or multimedia runtime claims until the QEMU desktop remains stable
  with the core apps.

---

## v1.5: Raspberry Pi 4 Bring-Up

Goal: boot on real Raspberry Pi 4 hardware with a useful serial/debug path.

Candidate work:

- Confirm linker/load address, boot handoff, and early UART on hardware.
- Bring up board memory map and MMIO ranges without QEMU assumptions leaking
  into generic kernel code.
- Wire PCIe host bridge enough for the VL805 xHCI controller to appear.
- Reuse the existing USB HID stack once the controller is visible.
- Document every board-specific difference in `docs/PORTING.md`.

Exit criteria:

- real hardware reaches a clear serial milestone;
- hardware failures are distinguishable from QEMU-only assumptions;
- QEMU v1.0 gates still pass.

---

## v2.0: Engine And Multimedia Runtime

Goal: build the foundation for graphical, audio, and interactive demos on top
of the stable desktop.

Candidate work after the minimal userland track proves its shape:

- audio output path;
- timer/input APIs suitable for interactive apps;
- richer drawing primitives or a small userland graphics helper;
- asset loading conventions;
- sample demos that stress windowing, input, storage, and timing.

This remains future work until the QEMU desktop is stable and the minimal
engine track proves what the kernel is actually missing.

---

## Version Targets

| Version | Status | Focus | Exit Signal |
| --- | --- | --- | --- |
| v0.9 | current | QEMU desktop baseline | Current gates pass; tech-debt review closed |
| v1.0 | next | Stable QEMU desktop release | All gates plus one visible desktop pass |
| v1.1 | flexible | Desktop-core usability / app polish | Files, shell, editor, and panel pass visible desktop workflows |
| v1.5 | planned | Raspberry Pi 4 hardware bring-up | Real hardware reaches documented serial/boot milestone |
| v2.0 | future | Engine and multimedia runtime | Interactive demos on the stable desktop base |

---

## Rules For New Work

- Keep the syscall ABI pinned in `kernel/syscall_numbers.h`,
  `docs/SYSCALLS.md`, wrappers, and tests in the same change.
- Prefer host tests for pure C logic.
- Use QEMU gates for behavior that host tests cannot cover.
- Keep board-specific code behind the board layer.
- Keep the kernel small enough to understand and under the configured size
  limit.
