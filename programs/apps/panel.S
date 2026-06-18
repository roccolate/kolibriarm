// KolibriARM app: panel
//
// Draws a taskbar at the bottom of the screen with one launcher button
// per registered app and a running-apps row underneath. On click:
//   - if y is inside the launcher button row, sys_spawn the app behind
//     that button.
//   - if y is inside the running-apps row, raise the first window owned
//     by the running pid behind that slot via SYS_WINDOW_FOCUS.
//
// The running-apps table is rebuilt periodically by polling
// SYS_PROCLIST and resolving each pid through SYS_WINDOW_FOR_PID. The
// panel itself is filtered out by comparing pid against sys_getpid().
// This implements the Phase 10.3 exit criterion "the taskbar shows
// running apps by pid; clicking an entry raises its window".

#define SYS_EXIT         1
#define SYS_YIELD        2
#define SYS_GETPID       3
#define SYS_WRITE       43
#define SYS_SPAWN        4
#define SYS_PROCLIST   102
#define SYS_WINDOW_CREATE    70
#define SYS_WINDOW_DRAW_RECT 73
#define SYS_WINDOW_DRAW_TEXT 72
#define SYS_WINDOW_REDRAW    76
#define SYS_WINDOW_EVENT     74
#define SYS_WINDOW_DESTROY  71
#define SYS_WINDOW_FOCUS     77
#define SYS_WINDOW_FOR_PID   78

// Geometry constants for a 640x480 virtio-gpu framebuffer.
.equ SCREEN_W,      640
.equ SCREEN_H,      480
.equ PANEL_H,       56
.equ PANEL_Y,       (SCREEN_H - PANEL_H)        // 424
.equ BTN_H,         24
.equ BTN_Y,         (PANEL_Y + 4)               // 428
.equ BTN_W,         80
.equ BTN_GAP,       8
.equ BTN_COUNT,     8

// Running-apps row sits below the buttons. RUN_ROW_Y is panel-local.
.equ RUN_ROW_Y,     32
.equ RUN_ROW_H,     (PANEL_H - RUN_ROW_Y)       // 24
.equ RUN_SLOT_W,    150
.equ RUN_SLOT_GAP,  4
.equ RUN_SLOT_COUNT, 4
// Background pollig cadence: refresh once every REFRESH_PERIOD loop
// iterations so each poll costs us only ~1% of CPU.
.equ REFRESH_PERIOD, 100

// Layout offsets within the panel stack frame (set up in panel_start).
.equ OFF_RUNNING,    0
.equ RUNNING_STRIDE, 8                       // {pid, window_id}
.equ OFF_NAMES,      (RUN_SLOT_COUNT * 8)    // 32, 16-byte slots
.equ NAME_STRIDE,    16
.equ OFF_PROCLIST,   (OFF_NAMES + RUN_SLOT_COUNT * NAME_STRIDE)  // 96
.equ PROC_STRIDE,    24
.equ PROCLIST_MAX,   8
.equ OFF_PROCLIST_END, (OFF_PROCLIST + PROCLIST_MAX * PROC_STRIDE) // 288
.equ OFF_EVENTS,     OFF_PROCLIST_END        // 288
.equ EVENT_CAP,      8
.equ OFF_EVENTS_END, (OFF_EVENTS + EVENT_CAP * 12) // 384
.equ OFF_YIELD_CTR,  OFF_EVENTS_END          // 384
// Hover state: signed int32 that encodes which button the cursor is
// currently over (-1 for none). The panel_on_move updates it on
// transitions and repaints the two affected buttons.
.equ OFF_HOVER,      (OFF_YIELD_CTR + 8)     // 392
.equ FRAME_BYTES,    (OFF_HOVER + 16)        // 408, round up

// Colors (BGRA in little-endian: 0xAABBGGRR).
.equ COL_BG,         0xff202428
.equ COL_BORDER,     0xff808080
.equ COL_BTN_BG,     0xff3a4658
.equ COL_BTN_HI,     0xff5870a0
.equ COL_BTN_TXT,    0xffe0e8f0
.equ COL_TITLE_TXT,  0xff8090a0
.equ COL_RUN_BG,     0xff182028
.equ COL_RUN_ACTIVE, 0xff506880
.equ COL_RUN_TXT,    0xffc0d0e0

.section .user.image.header, "a"
.global panel_image_header
.type panel_image_header, %function
panel_image_header:
    .word 0x31494c4b
    .hword 80
    .hword 1
    .quad panel_image_end - panel_image_header
    .quad panel_start - panel_image_header
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0

.section .user.image.text, "ax"
.global panel_start
.type panel_start, %function

panel_start:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    sub sp, sp, #FRAME_BYTES

    // Zero the running-apps table so empty slots are easy to detect.
    mov x9, #OFF_RUNNING
    mov x10, #RUN_SLOT_COUNT
1:  str xzr, [sp, x9]
    add x9, x9, #8
    sub x10, x10, #1
    cbnz x10, 1b

    mov x0, #1
    adr x1, panel_banner
    bl common_write_cstr

    // Create panel window at bottom of screen.
    mov x0, #0                          // x
    mov x1, #PANEL_Y                    // y
    mov x2, #SCREEN_W                   // w
    mov x3, #PANEL_H                    // h
    movz x4, #0xff20, lsl #16
    movk x4, #0x2428                    // bg
    movz x5, #0xff80, lsl #16
    movk x5, #0x8080                    // border
    adr x6, panel_title
    mov x8, #SYS_WINDOW_CREATE
    svc #0
    cmp x0, #0
    b.lt panel_fail
    mov x19, x0                         // x19 = window_id

    // Cache our own pid so we can filter ourselves out of sys_proclist.
    mov x8, #SYS_GETPID
    svc #0
    mov x25, x0                         // x25 = panel_pid

    bl panel_redraw_all

    mov x0, #1
    adr x1, panel_ready
    bl common_write_cstr

    // One-shot self-test: synthesize a click on the first button and route
    // it through the same path the event loop uses. Removed by build tag.
#ifdef PANEL_SELFTEST
    mov w26, #8                         // click x=8 (inside button 0)
    mov w27, #(BTN_Y - PANEL_Y + 4)
    bl panel_on_click
    b panel_loop
#endif

    add x20, sp, #OFF_EVENTS            // x20 = event buffer base

panel_loop:
    mov x0, x19
    mov x1, x20
    mov x2, #EVENT_CAP
    mov x8, #SYS_WINDOW_EVENT
    svc #0
    mov x21, x0                         // n events
    cmp x21, #0
    b.le panel_poll

    mov x22, #0
1:  cmp x22, x21
    b.ge panel_poll

    // Each event is 12 bytes (3 x uint32_t).
    mov x23, x22
    mov x24, #12
    mul x23, x23, x24
    add x24, x20, x23

    ldr w25, [x24]                      // type
    ldr w26, [x24, #4]                  // data1
    ldr w27, [x24, #8]                  // data2

    cmp w25, #3                         // GUI_EVENT_MOUSE_CLICK
    b.ne 2f
    // data1 = abs x, data2 = abs y; convert to panel-local.
    sub w26, w26, #0                    // panel is at x=0
    sub w27, w27, #PANEL_Y
    bl panel_on_click
    b 9f

2:
    cmp w25, #4                         // GUI_EVENT_MOUSE_MOVE
    b.ne 9f
    sub w26, w26, #0
    sub w27, w27, #PANEL_Y
    bl panel_on_move

9:  add x22, x22, #1
    b 1b

// panel_poll: refresh the running-apps table every REFRESH_PERIOD loop
// iterations. Uses a 64-bit counter at OFF_YIELD_CTR.
panel_poll:
    ldr x9, [sp, #OFF_YIELD_CTR]
    add x9, x9, #1
    str x9, [sp, #OFF_YIELD_CTR]
    cmp x9, #REFRESH_PERIOD
    b.lt panel_yield
    str xzr, [sp, #OFF_YIELD_CTR]
    bl panel_refresh_running

panel_yield:
    mov x0, #0
    mov x8, #SYS_YIELD
    svc #0
    b panel_loop

panel_fail:
    mov x0, #1
    adr x1, panel_fail_msg
    bl common_write_cstr
    // Instead of exiting, park the panel in a yield loop so the
    // kernel debug console (k>) gets a chance to run once the user
    // demo hands the CPU back. This keeps the process table populated
    // for 'ps' / 'proclist' queries even when GUI setup fails.
    mov x0, #0
    mov x8, #SYS_GETPID
    svc #0
    mov x19, x0                         // panel_pid (still useful for k> output)
panel_fail_loop:
    mov x0, #0
    mov x8, #SYS_YIELD
    svc #0
    b panel_fail_loop

// panel_redraw_all: redraw the panel background, top border, and all buttons.
panel_redraw_all:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    // Background fill (already part of bg, but redraw to clear prior state).
    mov x0, x19
    mov x1, #0
    mov x2, #0
    movz x3, #0xff20, lsl #16
    movk x3, #0x2428
    movz x4, #SCREEN_W & 0xffff, lsl #0
    mov x5, #PANEL_H
    mov x8, #SYS_WINDOW_DRAW_RECT
    svc #0

    // Top border line at y=0.
    mov x0, x19
    mov x1, #0
    mov x2, #0
    mov x3, #SCREEN_W
    mov x4, #1
    movz x5, #0xff50, lsl #16
    movk x5, #0x6070
    mov x8, #SYS_WINDOW_DRAW_RECT
    svc #0

    // Draw each button.
    mov x26, #0                         // button index
panel_btn_loop:
    cmp x26, #BTN_COUNT
    b.ge panel_btn_done

    // Is this button currently the hovered one?
    ldr w9, [sp, #OFF_HOVER]
    cmp w26, w9
    b.ne panel_btn_no_hover
    mov w10, #1                         // w10 = hovered
    b panel_btn_paint
panel_btn_no_hover:
    mov w10, #0
panel_btn_paint:
    mov x0, x26
    mov x1, x10
    bl panel_redraw_button
    add x26, x26, #1
    b panel_btn_loop
panel_btn_done:

    // Separator line above the running-apps row.
    mov x0, x19
    mov x1, #0
    mov x2, #(RUN_ROW_Y - 1)
    mov x3, #SCREEN_W
    mov x4, #1
    movz x5, #0xff50, lsl #16
    movk x5, #0x6070
    mov x8, #SYS_WINDOW_DRAW_RECT
    svc #0

    bl panel_redraw_running

    // Trigger redraw.
    mov x0, x19
    mov x8, #SYS_WINDOW_REDRAW
    svc #0

    ldp x29, x30, [sp], #16
    ret

// panel_button_x: x0 = index in, x0 = button x (panel-local).
panel_button_x:
    mov x1, #BTN_W
    add x1, x1, #BTN_GAP
    mul x0, x0, x1
    add x0, x0, #4
    ret

// panel_label_ptr: x0 = index, x0 = pointer to label string.
panel_label_ptr:
    adr x1, panel_labels
    mov x2, #10
    mul x0, x0, x2
    add x0, x0, x1
    ret

// panel_redraw_button: x0 = button index, x1 = hovered (0/1). Paints the
// button background (BTN_BG or BTN_HI) and the centered label.
panel_redraw_button:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    mov x26, x0                         // button index
    mov x27, x1                         // hovered flag

    bl panel_button_x                    // x0 = button x
    mov x25, x0                         // x25 = button x (preserve across SVCs)

    // Background fill: BTN_HI when hovered, BTN_BG otherwise.
    movz w5, #0xff3a, lsl #16
    movk w5, #0x4658                    // BTN_BG = 0xff3a4658
    cbz w27, 1f
    movz w5, #0xff58, lsl #16
    movk w5, #0x70a0                    // BTN_HI = 0xff5870a0
1:
    mov x0, x19
    mov x1, x25
    mov x2, #BTN_Y - PANEL_Y
    mov x3, #BTN_W
    mov x4, #BTN_H
    mov x8, #SYS_WINDOW_DRAW_RECT
    svc #0

    // Label.
    mov x0, x19
    mov x1, x25
    add x1, x1, #8
    mov x2, #BTN_Y - PANEL_Y
    add x2, x2, #6
    movz x3, #0xffe0, lsl #16
    movk x3, #0xe8f0
    mov x0, x26
    bl panel_label_ptr
    mov x4, x0
    mov x0, x19
    mov x1, x1
    mov x2, x2
    mov x3, x3
    mov x8, #SYS_WINDOW_DRAW_TEXT
    svc #0

    ldp x29, x30, [sp], #16
    ret

// panel_redraw_running: paint each running-apps slot. Empty slots show
// the dim COL_RUN_BG color; populated slots show the brighter
// COL_RUN_ACTIVE color and the app name centered.
panel_redraw_running:
    mov x28, sp                         // x28 = caller's sp (panel frame base + 16)
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    sub sp, sp, #48
    add x28, x28, #16                   // x28 = panel frame base (original sp - 16)

    mov x26, #0                         // slot index
panel_run_slot_loop:
    cmp x26, #RUN_SLOT_COUNT
    b.ge panel_run_slot_done

    bl panel_run_slot_x                 // x0 = slot x
    mov x27, x0                         // x27 = slot x

    // Determine background color: dim if empty, active if populated.
    mov x9, #RUNNING_STRIDE
    mul x9, x9, x26
    add x10, x28, #OFF_RUNNING
    add x10, x10, x9
    ldr x11, [x10]                      // x11 = slot.pid (full 8 bytes)
    movz w12, #0xff18, lsl #16
    movk w12, #0x2028
    cbz x11, 1f
    movz w12, #0xff50, lsl #16
    movk w12, #0x6880
1:
    // Background fill.
    mov x0, x19
    mov x1, x27
    mov x2, #RUN_ROW_Y
    mov x3, #RUN_SLOT_W
    mov x4, #RUN_ROW_H
    mov x5, x12
    mov x8, #SYS_WINDOW_DRAW_RECT
    svc #0

    // If slot is empty, skip the label.
    add x10, x28, #OFF_RUNNING
    add x10, x10, x9
    ldr x11, [x10]
    cbz x11, 2f

    // Build the name buffer pointer (callee-saved x25 holds panel_pid
    // in panel_poll; here we use it as a scratch slot to hand the
    // pointer to panel_strlen without trusting x10 across a bl).
    add x25, x28, #OFF_RUNNING + (RUN_SLOT_COUNT * RUNNING_STRIDE)
    mov x12, #16
    mul x12, x12, x26
    add x25, x25, x12
    mov x0, x25
    bl panel_strlen                     // x0 = strlen(name)
    mov x13, #6
    mul x13, x13, x0                    // text width in px
    mov x14, #RUN_SLOT_W
    sub x14, x14, x13
    lsr x14, x14, #1                    // left pad
    add x1, x27, x14
    mov x2, #RUN_ROW_Y
    add x2, x2, #8                      // baseline ≈ y+8 in 24 px tall row
    movz x3, #0xffC0, lsl #16
    movk x3, #0xD0E0
    mov x4, x25
    mov x0, x19
    mov x8, #SYS_WINDOW_DRAW_TEXT
    svc #0
2:
    add x26, x26, #1
    b panel_run_slot_loop
panel_run_slot_done:

    add sp, sp, #48
    ldp x29, x30, [sp], #16
    ret

// panel_run_slot_x: x0 = slot index in, x0 = slot x (panel-local).
panel_run_slot_x:
    mov x1, #RUN_SLOT_W
    add x1, x1, #RUN_SLOT_GAP
    mul x0, x0, x1
    add x0, x0, #4
    ret

// panel_strlen: x0 = ptr, x0 = strlen (excluding terminator).
panel_strlen:
    mov x1, x0
    mov x2, #0
1:  ldrb w3, [x1, x2]
    cbz w3, 2f
    add x2, x2, #1
    cmp x2, #15
    b.lt 1b
2:  mov x0, x2
    ret

// panel_refresh_running: poll SYS_PROCLIST, resolve each non-self pid
// through SYS_WINDOW_FOR_PID, then call panel_redraw_running. The
// running-apps table is laid out as RUN_SLOT_COUNT * RUNNING_STRIDE
// bytes; each slot is {pid(u32), window_id(u32)} here. The name from
// proclist is copied into a parallel name buffer adjacent to the slot
// table for panel_redraw_running to render.
panel_refresh_running:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    add x28, x29, #16                   // x28 = panel frame base

    // Zero the slot table and name buffer first; entries we cannot
    // repopulate stay empty.
    mov x9, #0
1:  cmp x9, #RUN_SLOT_COUNT
    b.ge 2f
    mov x10, #RUNNING_STRIDE
    mul x10, x10, x9
    add x11, x28, #OFF_RUNNING
    add x11, x11, x10
    str xzr, [x11]
    add x9, x9, #1
    b 1b
2:
    mov x9, #0
3:  cmp x9, #RUN_SLOT_COUNT
    b.ge 4f
    mov x10, #NAME_STRIDE
    mul x10, x10, x9
    add x11, x28, #OFF_NAMES
    add x11, x11, x10
    str xzr, [x11]
    str xzr, [x11, #8]
    add x9, x9, #1
    b 3b
4:

    // Issue SYS_PROCLIST into the buffer at OFF_PROCLIST.
    add x0, x28, #OFF_PROCLIST
    mov x1, #PROCLIST_MAX
    mov x8, #SYS_PROCLIST
    svc #0
    cmp x0, #0
    b.le panel_refresh_running_done      // 0 or negative => nothing to do

    mov x22, x0                         // x22 = n entries

    // Walk proclist entries, skipping panel_pid. The first RUN_SLOT_COUNT
    // non-self entries populate the running-apps table.
    mov x23, #0                         // x23 = proclist index
    mov x24, #0                         // x24 = slot index (next free)
panel_refresh_loop:
    cmp x23, x22
    b.ge panel_refresh_done
    cmp x24, #RUN_SLOT_COUNT
    b.ge panel_refresh_done

    add x9, x28, #OFF_PROCLIST
    mov x10, #PROC_STRIDE
    mul x10, x10, x23
    add x9, x9, x10                     // x9 = &entry
    ldr w11, [x9]                       // entry.pid
    cmp w11, w25
    b.eq panel_refresh_skip              // skip self
    cbz w11, panel_refresh_skip          // skip empty slots

    // Resolve window_id via SYS_WINDOW_FOR_PID(pid, 0).
    mov x0, x11
    mov x1, #0
    mov x8, #SYS_WINDOW_FOR_PID
    svc #0
    mov x12, x0
    cmp x12, #0
    b.lt panel_refresh_skip              // ERR_NOENT or negative => skip

    // Store pid and window_id into the slot.
    mov x10, #RUNNING_STRIDE
    mul x10, x10, x24
    add x11, x28, #OFF_RUNNING
    add x11, x11, x10
    str w11, [x11]
    str w12, [x11, #4]

    // Copy the app name (16 bytes, null-terminated) from the proclist
    // entry into the slot's name slot.
    mov x10, #NAME_STRIDE
    mul x10, x10, x24
    add x13, x28, #OFF_NAMES
    add x13, x13, x10
    add x15, x9, #8                     // x15 = &entry.name[0]
    mov x14, #0
5:  cmp x14, #16
    b.ge 6f
    ldrb w16, [x15, x14]
    strb w16, [x13, x14]
    add x14, x14, #1
    b 5b
6:

    add x24, x24, #1

panel_refresh_skip:
    add x23, x23, #1
    b panel_refresh_loop

panel_refresh_done:
    bl panel_redraw_running
    mov x0, x19
    mov x8, #SYS_WINDOW_REDRAW
    svc #0

panel_refresh_running_done:
    ldp x29, x30, [sp], #16
    ret

// panel_on_click: w26 = panel-local x, w27 = panel-local y.
// Determine which region was clicked and route accordingly:
//   - button row  -> sys_spawn the app behind the button
//   - running row  -> SYS_WINDOW_FOCUS the slot's first window
//   - everything else (separator, gaps) -> ignore
panel_on_click:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    add x28, x29, #16                   // x28 = panel frame base

    cmp w27, #0
    b.lt panel_on_click_done

    // Button row first (smaller y).
    cmp w27, #(BTN_Y - PANEL_Y + BTN_H)
    b.lt panel_on_click_button

    // Running-apps row.
    cmp w27, #RUN_ROW_Y
    b.lt panel_on_click_done
    cmp w27, #(RUN_ROW_Y + RUN_ROW_H)
    b.ge panel_on_click_done

    // Compute slot index from x.
    mov x0, #RUN_SLOT_W
    add x0, x0, #RUN_SLOT_GAP
    udiv x1, x26, x0                    // x1 = slot index
    msub x2, x1, x0, x26                // x2 = remainder
    cmp x2, #RUN_SLOT_W
    b.ge panel_on_click_done
    cmp x1, #RUN_SLOT_COUNT
    b.ge panel_on_click_done

    // Look up the slot's window_id and raise it.
    mov x9, #RUNNING_STRIDE
    mul x9, x9, x1
    add x10, x28, #OFF_RUNNING
    add x10, x10, x9
    ldr w11, [x10, #4]                  // x11 = window_id
    cbz w11, panel_on_click_done         // empty slot, ignore

    mov x0, x11
    mov x8, #SYS_WINDOW_FOCUS
    svc #0
    b panel_on_click_done

panel_on_click_button:
    mov x0, #BTN_W
    add x0, x0, #BTN_GAP
    udiv x1, x26, x0                    // x1 = button index
    msub x2, x1, x0, x26                // remainder
    cmp x2, #BTN_W
    b.ge panel_on_click_done
    cmp x1, #BTN_COUNT
    b.ge panel_on_click_done

    mov x9, x1                          // x9 = button index

    // Print launch message.
    mov x11, x9
    mov x12, #10
    mul x11, x11, x12
    adr x13, panel_labels
    add x11, x11, x13                  // x11 = app name

    mov x0, #1
    adr x1, panel_launch_pre
    bl common_write_cstr
    mov x0, #1
    mov x1, x11
    bl common_write_cstr
    mov x0, #1
    adr x1, panel_launch_post
    bl common_write_cstr

    // Build path "/kolibri/<name>\0" on stack.
    sub sp, sp, #32
    mov x14, sp                         // x14 = path base (stable across writes)
    adr x0, panel_path_prefix
    mov x1, x14
    bl panel_strcpy_to                  // copies "/kolibri/" to [x1], x1 returns past end

    mov x0, x11
    mov x1, x1
    bl panel_append                     // appends app name
    mov x0, #0
    strb w0, [x1]

    mov x0, x14
    mov x1, #0
    mov x8, #SYS_SPAWN
    svc #0

    add sp, sp, #32

panel_on_click_done:
    ldp x29, x30, [sp], #16
    ret

// panel_on_move: w26 = panel-local x, w27 = panel-local y. Decides which
// button (if any) the cursor is currently over and repaints the
// affected buttons when the hover target changes. Stores the current
// hover index at OFF_HOVER.
panel_on_move:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    // Compute the new hover target. -1 means "no button".
    mov w9, #-1
    cmp w27, #0
    b.lt 1f
    cmp w27, #(BTN_Y - PANEL_Y + BTN_H)
    b.ge 1f
    mov x0, #BTN_W
    add x0, x0, #BTN_GAP
    udiv x1, x26, x0                    // x1 = button index
    msub x2, x1, x0, x26                // remainder
    cmp x2, #BTN_W
    b.ge 1f
    cmp x1, #BTN_COUNT
    b.ge 1f
    mov x9, x1
1:
    // Skip the repaint if nothing changed.
    ldr w10, [sp, #16 + OFF_HOVER]
    cmp w9, w10
    b.eq 9f

    // Repaint the old button (un-hovered) and the new button
    // (hovered), if each is in range. The empty-slot repaints reuse
    // the helper to keep one source of truth for button geometry.
    str w9, [sp, #16 + OFF_HOVER]

    // Old button: clear hover highlight.
    cmp w10, #0
    b.lt 2f
    cmp w10, #BTN_COUNT
    b.ge 2f
    mov x0, x10
    mov x1, #0
    bl panel_redraw_button
2:
    // New button: paint highlight.
    cmp w9, #0
    b.lt 3f
    cmp w9, #BTN_COUNT
    b.ge 3f
    mov x0, x9
    mov x1, #1
    bl panel_redraw_button
3:
    // Push the repaint to the framebuffer.
    mov x0, x19
    mov x8, #SYS_WINDOW_REDRAW
    svc #0
9:
    ldp x29, x30, [sp], #16
    ret

// panel_strcpy_to: copy x0 (null-terminated) to x1; x1 returns ptr to '\0'.
panel_strcpy_to:
    mov x2, x0
    mov x3, x1
1:  ldrb w4, [x2]
    cbz w4, 2f
    strb w4, [x3], #1
    add x2, x2, #1
    b 1b
2:  mov x0, x3                          // x0 = ptr to '\0' (return value)
    ret

// panel_append: append x0 (null-terminated) to x1; x0 returns ptr to '\0'.
panel_append:
    mov x2, x0
    mov x3, x1
1:  ldrb w4, [x2]
    cbz w4, 2f
    strb w4, [x3], #1
    add x2, x2, #1
    b 1b
2:  mov x0, x3
    ret

.section .user.image.rodata, "a"
panel_title:
    .ascii "panel\0"
panel_banner:
    .ascii "panel: starting\n\0"
panel_ready:
    .ascii "panel: ready\n\0"
panel_fail_msg:
    .ascii "panel: window create failed\n\0"
panel_launch_pre:
    .ascii "panel: launch \0"
panel_launch_post:
    .ascii "\n\0"
panel_path_prefix:
    .ascii "/kolibri/\0"

// One 10-byte slot per app, null-padded. Must match BTN_COUNT and order.
panel_labels:
    .ascii "hello\0\0\0\0\0"
    .ascii "loop\0\0\0\0\0\0"
    .ascii "fault\0\0\0\0\0"
    .ascii "shell\0\0\0\0\0"
    .ascii "editor\0\0\0\0"
    .ascii "monitor\0\0\0\0"
    .ascii "win\0\0\0\0\0\0\0"
    .ascii "clock\0\0\0\0\0"
panel_image_end:
