# Codex Notes for ArmoniOS

ArmoniOS is an early AArch64 bare-metal OS inspired by KolibriOS/MenuetOS.
Keep changes small, testable, and close to the existing freestanding C plus
small AArch64 assembly style.

## Current Direction

The QEMU `virt` target boots into a small graphical desktop:

- EL0 apps are freestanding C programs under `programs/apps/`.
- User images use the KLI1 flat format described by
  `kernel/user_image_format.h` and are exposed through bootfs/VFS under
  the `/armonios/<name>` app namespace.
- The panel is the first user process. It owns the taskbar, launches shell,
  editor, monitor, and clock, and recovers through the panel boot wrapper if it
  faults.
- Syscalls enter through `svc #0`, with the syscall number in `x8` and
  arguments in `x0..x6`.
- Process dispatch, syscall user-pointer validation, GUI ownership checks, and
  boot init status have shared helpers; do not reintroduce local copies.
- The original technical-debt review is closed. Track new debt in `docs/ROADMAP.md`,
  focused issues, or a fresh review document. The current quick-return cleanup
  target for v1.0 is `kernel/net/` plus `drivers/net/virtio_net.c`; leave
  `programs/apps/` stack/syscall-callsite review for v1.1 unless an app bug
  blocks QEMU stability.

## Boundaries

- Do not port KolibriOS x86 assembly literally. Port ideas, ABI shape,
  IPC/message concepts, GUI concepts, and small demos by reimplementing them
  for AArch64.
- Keep QEMU-specific addresses out of generic kernel code. Board-specific
  details belong behind `drivers/boards/<board>/` and `drivers/board.h`.
- Avoid libc, POSIX assumptions, hosted runtime behavior, or large abstractions.
- No vendored protocol stacks. The kernel network stack is hand-written under
  `kernel/net/`.
- Keep the kernel readable enough to understand in one sitting.
- Keep the kernel binary under `KERNEL_SIZE_LIMIT`; the size gate is tight.

## Build and Test

Use these before committing kernel changes:

```bash
make
make size
make -C tests test
```

Useful runtime checks:

```bash
make qemu
make qemu-fb
make qemu-fs-test
make qemu-usb
```

Use `make stack-check` after userland app changes, and `make help` for the
current target list.

## Implementation Style

- C code is freestanding and compiled with `-ffreestanding -nostdlib`.
- AArch64 assembly should be minimal, documented where control flow is subtle,
  and kept near the CPU boundary.
- Use existing modules before adding new ones: `kernel/mm`, `kernel/sched`,
  `kernel/timer`, `drivers/irq`, `drivers/uart`, `kernel/gui_*`.
- Add focused host tests for pure C logic when possible, especially memory,
  ABI, parser, and scheduler code.
- Do not hide important hardware behavior behind vague abstractions; name the
  architectural thing being controlled.

## Documentation

When changing direction or milestones, update `docs/ROADMAP.md`. When changing
build/run expectations, update `README.md`. When moving board-specific code,
update `docs/PORTING.md`. When changing syscall numbers or ABI shapes, update
`docs/SYSCALLS.md` in the same change.
