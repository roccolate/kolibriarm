Kernel technical-debt review

Scope: kernel/, drivers/, and the panel/apps support code. This
document is the execution guide for paying down the debt surfaced by the
panel/desktop work (BUG 1/2/3) and by a full read-through of kernel/*.c.
It is not a proposal to rewrite anything — every item below names a file,
the actual smell with a code excerpt, and a direction that keeps the
existing style (AGENTS.md: freestanding C, no libc, readable in one
sitting, no large abstractions).

How to use this doc: work top to bottom inside each phase. Each phase
ends in a state that passes make -C tests test and boots cleanly under
make qemu with all four apps. Do not start a phase before the previous
one is green — phases 2 and 3 specifically depend on phase 1 reducing
noise first. Phases are otherwise independent of any work happening on
XHCI, RPi4 bring-up, or networking; nothing here blocks those.

When an item is closed, move it under "Closed" at the bottom with the
commit/PR that closed it, instead of deleting it — the rationale for
why it mattered stays useful for whoever touches that code next.


Status snapshot

#ItemStatus0EL0 launch path host testspartially closed — see note1gui.c does five jobsopen2syscall dispatch has no validation tableopen3two gui_window_* naming conventionsopen4user-region validation duplicatedopen5KLI1/KOS load path scattered across 6+ filesopen6PROCESS_MAX_USER_REGIONS too tight, no guard pagesopen7kernel_main has no init-failure trackingopen8console.c/print.c overlapopen9magic numbers in build/linker scatteredopen10fat32.c untested at integration levelopen11driver headers split drivers/ vs kernel/ undocumentedopen12k> console is a hand-rolled strcmp chainopen13per-process image/stack slots are static BSSopen14recovery wrapper takes raw board paramsopen15app stack usage is unmeasuredopen16new — scheduling dispatch duplicated 3xopen17gui_resize_window rollback duplicated 3xopen18no project-wide compiler-attribute headeropen19dhcp.c has no host testopen20make output is noisy, no make helpopen

Item 0 note: tests/test_user_image_runtime.c already exists and covers
the image_size/rodata-tail contract that caused BUG 2. It does not
yet cover place_argv_on_stack's budget rejection. See Phase 0 below —
the remaining slice is small.


Phase 0 — Close the testing gap before refactoring anything

Why first: every later phase touches code that the EL0 launch path
depends on (panel_boot.c, gui.c, syscall.c). Phase 0 is the only
thing standing between "this refactor compiles" and "this refactor still
boots the panel correctly." It is small and almost done.

0a. Finish the place_argv_on_stack host test (item 16, remainder)

tests/test_user_image_runtime.c proves the image-load contract but does
not exercise panel_boot.c's place_argv_on_stack (lines 177-247):
budget enforcement (PANEL_BOOT_ARGV_MAX_STRINGS, PANEL_BOOT_ARGV_MAX_BYTES),
16-byte stack alignment, and the argc == 0 / argv_ptr != 0 rejection
path. None of this needs EL0 — place_argv_on_stack is pure C operating
on a uint8_t[] buffer that stands in for g_user_stacks[slot].

Direction: add test_panel_boot_argv.c (or extend
test_user_image_runtime.c) with:


a call with argc over PANEL_BOOT_ARGV_MAX_STRINGS → rejected,
a call whose total string bytes exceed PANEL_BOOT_ARGV_MAX_BYTES →
rejected,
a well-formed call → assert the returned argv_vaddr is 16-byte
aligned and that walking argv_out[0..argc-1] plus the NULL
sentinel reproduces the input strings byte-for-byte.


This requires exposing place_argv_on_stack past its current static
visibility (a #ifdef KOLIBRI_HOST_TEST guard around static, matching
whatever pattern the existing host tests already use for other
kernel-private statics — check test_process.c for the convention before
inventing a new one).

Exit criterion: make -C tests test includes and passes the new
argv-budget assertions. Stop here and confirm green before Phase 1.


Phase 1 — Shared syscall helpers (items 2, 4)

Why before the gui.c split: the helper this phase produces is what
the future kernel/gui_*.c files will call instead of re-deriving
ownership checks. Building it first means the split in Phase 3 has
something correct to use immediately instead of carrying the duplicated
logic into the new files and cleaning it up twice.

1a. sys_owner_window and buffer helpers (item 2)

$ grep -c "^static int64_t sys_\|case SYS_" kernel/syscall.c

37 sys_* functions, ~30 dispatch cases. The 9 window/cursor syscalls
(sys_window_draw_text, draw_rect, set_title, redraw, flush,
get_bounds, set_bounds, minimize, restore, state,
cursor_register_region — confirmed by reading kernel/syscall.c lines
560-939) each repeat this shape:

cif (process == 0 || window_id >= GUI_MAX_WINDOWS) return ERR_INVAL;
gui_window_t *window = &gui_desktop()->windows[window_id];
if (window->used == 0) return ERR_NOENT;
if (window->owner_pid != process->pid) return ERR_BADF;

Direction: a small kernel/syscall_helpers.{c,h} with:


gui_window_t *sys_owner_window(process_t *process, uint64_t window_id)
— does the lookup + ownership check above, sets errno-style out
parameter or returns 0 with the caller checking
sys_last_window_error() (pick whichever matches the project's
existing error-return convention — there is no errno global today,
so prefer returning the error code directly via an out-parameter
rather than introducing global state).
sys_user_str_in(ptr, len) / sys_user_buf_in(ptr, len) /
sys_user_buf_out(ptr, len) — single chokepoint for the validate-and-copy
glue currently duplicated across sys_write, sys_read, sys_open,
sys_spawn_argv, sys_window_set_title, and others.


Migrate one syscall per commit, gated by make -C tests test each time.
Order: start with sys_window_state (smallest body) to prove the helper
shape, then the rest. Collapses ~30 lines per syscall into ~5.

1b. Centralize user-region validation (item 4)

process_user_range_contains exists and is tested
(test_syscall_abi_user_range_validation_rejects_out_of_region), but its
use is inconsistent: sys_write/sys_read call it directly,
sys_open/sys_spawn_argv/sys_window_set_title go through
copy_user_cstr instead, and sys_window_event's buffer copy and
sys_window_get_bounds's output buffer don't validate at all today.

This is the same root cause as the BSS-static bug (item 13's stack
overflow case): no syscall path ever had to check the range, because the
faulting write never went through a syscall.

Direction: every user-pointer-taking syscall funnels through the
sys_user_buf_in/out/str_in helpers from 1a. This also lets
test_syscall_abi.c swap in one instrumented version and lock the
contract at a single chokepoint instead of ~15 separate call sites.

Exit criterion: grep -c "user_range_contains\|copy_user_cstr" kernel/syscall.c drops to near-zero outside syscall_helpers.c itself.


Phase 2 — Unify the "next runnable process" dispatch (new item, #16)

Why before the gui.c split, not after: this is the hottest path in
the kernel (every syscall, every IRQ, every fault crosses it) and the
least related to GUI code. Doing it while Phase 1 is fresh — and before
Phase 3 touches gui.c, which does not call this path — keeps the
two refactors from interfering with each other in the same review.

The same "find next runnable, activate it, or fall through to an exit
code" block is duplicated nearly verbatim in three places:

kernel/exceptions.c:101-111 (handle_user_fault):

cnext = process_next_runnable(current);
if (next != 0) {
    next->state = PROCESS_RUNNING;
    process_set_current(next);
    process_activate_context(next, frame);
    return;
}
frame->x[0] = USER_FAULT_EXIT_CODE;
frame->elr = el0_return_address();
frame->spsr = SPSR_EL1H_MASKED;

kernel/syscall.c:951-962 (sys_exit) — identical shape, different
exit value (code instead of USER_FAULT_EXIT_CODE).

kernel/irq.c:64-73 (irq_handler_frame) — the one real variation:
preserves current->state = PROCESS_READY for a preempted process
instead of leaving it to be overwritten by an exit path.

Symptom: a future scheduling-policy change (priorities, per-process
quantum) means finding and updating three copies and trusting that the
one intentional difference between them was preserved on purpose, not by
accident.

Direction: one function in kernel/process.c:

c// Returns 1 if it activated another process into `frame` (frame
// already mutated, caller returns immediately). Returns 0 if no
// other process was runnable — caller is responsible for writing
// its own exit value into frame->x[0]/elr/spsr.
int process_dispatch_next(process_t *current, exception_frame_t *frame,
                          process_exit_policy_t policy);

policy captures the one real difference (PROCESS_DISPATCH_PREEMPT
leaves the outgoing process READY; PROCESS_DISPATCH_EXIT does not,
since the process is becoming a zombie). Migrate exceptions.c,
syscall.c::sys_exit, and irq.c to call it, one call site per commit.

Caution: this is the hottest path in the kernel. Each commit needs a
manual make qemu run with all four apps opened and closed via
title-bar close, not just the host suite — the host suite cannot exercise
real EL0 preemption.

Exit criterion: process_next_runnable( appears in exactly one call
site (inside process_dispatch_next itself) by grep -rn.


Phase 3 — Split kernel/gui.c (items 1, 3, 17)

Note: cuando llegues a la Fase 3 de la propuesta de reescritura (el split de gui.c), aprovecha para que gui_hit_test_decoration() devuelva geometría pura y deje la decisión de qué significa cada zona (¿esto es un botón de cerrar? ¿dónde está exactamente?) en un lugar que en el futuro pueda volverse configurable sin tocar el kernel — aunque hoy esa configuración no exista. Es la diferencia entre "kernel decide forma" (mecanismo, queda) y "kernel decide significado" (política, debería poder migrar sin que sea una reescritura mayor).

Why now: Phases 1-2 already removed the syscall-side duplication that
would otherwise leak into the new files. This is the largest phase —
budget it on its own, one sub-step at a time, most-isolated-first.

$ wc -l kernel/gui.c
1884 kernel/gui.c

gui.c mixes five jobs: a fixed pool of gui_window_t slots and their
lifecycle (create/destroy/move/resize/focus/minimize/restore), a
per-window backing-buffer allocator, a per-window event queue, damage-rect
tracking for the partial-repaint compositor, and input dispatch (drag
state, button masks, cursor regions, hit-testing). Confirmed while
reading gui_handle_input (lines 1763-1866): the
INPUT_EVENT_MOUSE_BUTTON case alone is an ~80-line block doing
geometric hit-testing for three decoration buttons (minimize/maximize/close)
with the same bounds check repeated three times, mixed with drag-start
logic in the same block.

Symptom: the cursor-region work (commit b12761b) touched
gui_create_window_for_pid, gui_destroy_window,
gui_refresh_cursor_shape, the header, and every test that constructs a
window by hand. The file is past the "fits in one sitting" line
AGENTS.md calls out.

Split order (most-isolated dependency first)


kernel/gui_events.{c,h} — per-window event queue
(gui_window_push_event/pop_event). Zero framebuffer dependency,
easiest to host-test in isolation. Do this one first to prove the
split mechanics (header re-export, build rule, test migration) on the
smallest piece.
kernel/gui_input.{c,h} — drag state, cursor shape, button masks,
cursor regions, and a new gui_hit_test_decoration(window, x, y) -> {NONE, MINIMIZE, MAXIMIZE, CLOSE} that replaces the 80-line
if/else-chain in gui_handle_input. This function is pure geometry —
host-testable without a framebuffer, which the current code is not.
kernel/gui_backing.{c,h} — per-window backing buffer allocator
and capacity tracking. While here, fold in item 17: gui_resize_window,
gui_destroy_window, and user_image_prepare_process each duplicate
the same unmap/realloc/restore-on-failure dance for a window's
backing buffer. Replace with one gui_backing_realloc(window, new_size) returning 0/-1, atomic — if it fails, the old backing is
untouched. Three rollback paths become one.
kernel/gui_pool.{c,h} — window struct lifecycle
(create/destroy/move/resize/focus/minimize/restore/bounds).
kernel/gui_compositor.{c,h} — damage rects, gui_draw,
gui_render, the g_gui_desktop/gui_desktop() singleton. Last,
because the most other code (syscall handlers, kernel_main) calls
into this directly.


The shared structs (gui_window_t, gui_desktop_t, gui_event_t) stay
in gui.h, which keeps re-exporting everything from the five new files —
no call site outside kernel/gui*.c changes.

While splitting, also resolve item 3 (naming collision)

gui.h currently exports gui_window_set_title (syscall-facing,
ownership-checked) and gui_set_window_title (kernel-internal helper,
no ownership check) — names that give no hint which is which.  Same
problem with gui_set_window_title_bar vs gui_window_set_title. As
each function moves into its new file in steps 1-5 above, rename the
kernel-internal helpers to a gui_window_*_internal suffix or move them
behind a kernel/gui_internal.h not included by syscall.c. Do this
rename as part of the same commit that moves the function — don't do a
separate blanket rename pass, since that touches every call site twice.

Exit criterion: kernel/gui.c no longer exists as a single file;
each gui_*.c file is under ~400 lines; make -C tests test and
make qemu (all four apps, opened/closed/dragged) both green.


Phase 4 — kernel_main init-failure tracking (item 7)

Why after 1-3, not before: this phase is mechanical once the rest of
the kernel's control flow is legible. Doing it first would mean writing
the status table against code that's about to be restructured underneath
it.

Of kernel_main's six post-MMU phases (init_vfs, init_timer_irq_demo,
probe_storage, init_display, init_network, init_input), only
probe_storage's return value is used for anything (choosing FAT32 vs.
bootfs as the image source). The other five print a UART line and
continue regardless of outcome. init_vfs does not even return int.

Direction: an init_status_t table, one entry per phase (board, dtb,
pmm, vmm, console, vfs, gpu, net, input, sched, panel), populated by each
phase as it runs. kernel_main checks the table once at the end of the
boot sequence and either continues normally or prints a single
"boot failed at <phase>" line instead of a scattered log with no
hierarchy. Wire the same table into kernel/console.c's k> so a
debug-console user can ask "sched: not initialised" instead of the
console behaving inconsistently depending on which phase silently failed.

Exit criterion: init_vfs, init_network, probe_storage, and
init_display all return a status that lands in the table; k> exposes
a status command reading it.


Phase 5 — panel_boot.c cleanup (items 5, 9, 13)

Why last: this is the highest-risk-of-silent-regression phase — it's
the same file that produced BUG 1/2 — so it goes last, after Phase 0's
test coverage is in place as a safety net, and after Phases 1-4 are not
competing for review attention at the same time.

5a. Move static slots into PMM-backed allocations (item 13)

cstatic uint8_t g_user_image_slots[PROCESS_MAX_PROCESSES][PANEL_BOOT_IMAGE_SLOT_SIZE]
    __attribute__((aligned(4096)));
static uint8_t g_user_stacks[PROCESS_MAX_PROCESSES][PANEL_BOOT_STACK_SIZE]
    __attribute__((aligned(4096)));

16 * (8192 + 4096) = 196 KB of static BSS regardless of how many
processes are actually alive. Fine for one panel plus one shell; tight
once multiprocess/networking work lands.

Direction: pull both arrays out of BSS into pmm_alloc_page-backed
allocations, one fresh page-aligned block per spawn, freed on exit. This
also resolves the "first fault leaves stale data in the slot" question
from BUG 2 — page-aligned zeroing on alloc instead of relying on
zero-initialized BSS that was already reused by a previous process.

5b. Consolidate magic numbers (item 9)

linker.ld:            . = 0x40080000
linker.ld:            . += 0x4000              # __stack_top
kernel/panel_boot.c:   PANEL_BOOT_IMAGE_VA_BASE   0x400000
kernel/panel_boot.c:   PANEL_BOOT_STACK_VA_BASE   0x800000
kernel/panel_boot.c:   PANEL_BOOT_STACK_SIZE      4096
kernel/panel_boot.c:   PANEL_BOOT_IMAGE_SLOT_SIZE 8192
kernel/syscall.c:      USER_FAULT_EXIT_CODE 0xfffffffffffffff0
kernel/exceptions.c:   SPSR_EL1H_MASKED     0x3c5

The relationship between 0x40080000 and PANEL_BOOT_IMAGE_VA_BASE + PANEL_BOOT_IMAGE_SLOT_SIZE * PROCESS_MAX_PROCESSES is nowhere expressed
in code — bumping the image base by accident would silently overlap the
kernel without anyone noticing.

Direction: a single kernel/layout.h with all of the above plus
_Static_asserts checking the non-overlap invariants directly (e.g.
PANEL_BOOT_IMAGE_VA_BASE + PANEL_BOOT_IMAGE_VA_STRIDE * PROCESS_MAX_PROCESSES <= PANEL_BOOT_STACK_VA_BASE).

5c. Replace the hand-rolled path parser (new sub-item under 5)

kolibri_spawn_vfs (lines 249-318) parses the /kolibri/<name> prefix
with nine sequential character comparisons:

cif (path[0] != '/' || path[1] != 'k' || path[2] != 'o' ||
    path[3] != 'l' || path[4] != 'i' || path[5] != 'b' ||
    path[6] != 'r' || path[7] != 'i' || path[8] != '/' ||
    path[9] == '\0') {
    return -1;
}

This duplicates string-matching logic that VFS path handling already has
to do elsewhere. Direction: a small vfs_strip_prefix(path, "/kolibri/") -> const char* | NULL helper, host-tested directly, used here instead of
the inline comparison chain.

5d. Loader path consolidation (item 5)

kernel/user_image.c          — flat-header parser + loader
kernel/boot_program.c        — bootfs-name -> __app_*_blob symbols
kernel/bootfs.c               — bootfs file table
kernel/panel_boot.c           — load + map the per-process image
kernel/panel_boot_recovery.c  — recovery policy
programs/libkarm/crt0.S       — _start entry
programs/apps/image.ld        — linker script collecting .user_image
programs/apps/*_header.S      — KLI1 header
programs/apps/*_end.S         — image_end marker

Six C/header files plus two asm files collaborate to produce one loaded
process. Phase 0 already added the host-test contract that would have
caught BUG 2 (image_size vs. rodata tail). What's still missing is a
single source of truth for the binary layout itself: image.ld encodes
it, but nothing documents it as a contract other code must match.

Direction: kernel/user_image_format.h documenting the KLI1 binary
layout (header fields, sizes, entry offsets, where rodata must live)
with _Static_asserts pinning struct sizes — the single owner of "is
this image well-formed," referenced (not re-derived) by
user_image.c, the host tests from Phase 0, and image.ld's comments.

5e. Recovery wrapper signature (item 14)

panel_boot_recovery_decide is pure C and host-tested — good.
panel_boot_run_with_recovery is not: it takes memory_base,
memory_size, map_mmio only to forward them, and prints to UART
directly on every iteration. Direction:

ctypedef uint64_t (*panel_boot_run_fn)(void *ctx);
panel_boot_recovery_action_t panel_boot_recovery_run(
    panel_boot_run_fn run, void *ctx,
    void (*log)(const char *line));

kernel_main builds a small panel_boot_ctx, passes a log callback, and
the recovery module stops pulling in uart/pl011.h directly — easier to
test, easier to disable in release builds, easier to swap for a "reboot"
policy later.

Exit criterion: make -C tests test green; make qemu panel boot
survives a forced fault (existing recovery test) with the new signature;
grep -n "0x400000\|0x800000" kernel/panel_boot.c returns nothing
(all moved to layout.h).


Backlog — not yet phased, pick up opportunistically

These don't block the phases above and don't block each other. Good
candidates for a short side session between phases.

process_user_region_t is over-restrictive (item 6)

c#define PROCESS_MAX_USER_REGIONS 4U

Apps wanting "stack + image + scratch + mmap" already hit the limit
(test_process_alloc_user_region_respects_region_limit locks it in). No
guard-page handling exists — a process running past its 4 KB stack isn't
caught until the address hits another mapping. This is how the "FAR
0x891000" faults slipped through: a deeply-nested panel call walked off
the top of its stack into the next slot's unmapped region.

Direction: bump to 6-8 regions and add stack-guard pages (an unmapped
page above each stack), or explicitly document that 4 KB EL0 stacks are
tight and any state over ~1 KB must live in the image's rodata.

console.c/print.c overlap (item 8)

Both implement put-style helpers and hex-print routines independently.
Direction: print.c keeps kernel-wide helpers (panic/boot path);
console.c keeps the k> line-discipline loop. Remove the duplicate
print_hex64/puts pair once the split is enforced.

Driver header split undocumented (item 11)

drivers/ vs kernel/ has no written rule, and kernel/gui.h already
references drivers/fb/fb.h's fb_t, leaking the boundary. Direction:
write down "kernel/ = CPU-state/scheduler/syscalls/platform-agnostic
VFS-FS-GUI; drivers/ = anything touching hardware registers," then fix
the one known leak (gui.h → fb.h) to match.

k> console command table (item 12)

Each command (help, ps, mem, ticks, storage, fb, mouse,
click) is a hand-rolled strcmp branch. Direction: a
struct k_command { name, fn, help }[], iterated for dispatch and for
auto-generating help. ~30-line change; removes the class of bug where
help text and dispatch logic drift apart.

App stack usage unmeasured (item 15)

shell_state_t was ~17 KB and silently overflowed the kernel's 4 KB
stack until the recent fix; there's no host-side measurement of any
app's worst-case stack frame. Direction: a make stack-check target
compiling each app with -fstack-usage, summing per-function frames on
the worst-path call chain, asserting the total stays under
PANEL_BOOT_STACK_SIZE.

FAT32 integration testing (item 10)

test_fat32.c is thorough on the in-memory parser but the kernel-side
wiring (fat32_mount_vfs, fat32_set_write_sector, the
board_storage_* plumbing) is only ever exercised at QEMU runtime.
Direction: either a fake board_storage_*_stub returning a fixed sector
for host tests, or a make qemu-fs-test target that boots QEMU headless
with a generated FAT32 image and greps UART output for the expected
sequence.

Compiler-attribute header (item 18)

Several static functions in gui.c are single-TU and trigger
-Wunused-function, worked around ad hoc with
__attribute__((unused)). Direction: a kernel/kernel_compiler.h with
project-wide __unused, __printf_format, __packed, etc.

dhcp.c host test (item 19)

kernel/net/dhcp.c parses DHCP options with manual byte-pointer walks
and has zero host coverage — not even listed in the test Makefile.
Direction: extract the option parser into a pure function, feed it
crafted option bytes from a ~30-line host test.

make help (item 20)

Each invocation produces ~80 lines of gcc output with no -s shorthand
and no target listing. Direction: a make help target enumerating the
documented targets (qemu, qemu-fb, qemu-fb-visible, qemu-usb,
size, libkarm, apps, clean, entry-check).


Closed

(move items here as they land, with the commit/PR reference — keeps the
"why this mattered" context instead of letting git log be the only
record)