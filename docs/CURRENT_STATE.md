# Current State

This is a focused audit of the desktop and GUI-related pieces in the current
tree. It is intentionally narrower than the full roadmap.

## Kernel GUI

The active GUI code lives in `kernel/gui.c` and `kernel/gui.h`. It is still a
kernel-owned compositor, not a desktop server.

Current desktop/window functions:

- `gui_init` initializes a fixed `gui_desktop_t` over an `fb_t`.
- `gui_create_window` creates an ownerless debug/demo window.
- `gui_create_window_for_pid` creates a window owned by a process pid.
- `gui_destroy_window` clears a window slot.
- `gui_set_window_title` stores a fixed-size title.
- `gui_move_window`, `gui_focus_window`, and `gui_focus_window_ensure` update
  window position and keyboard focus.
- `gui_window_draw_rect`, `gui_window_draw_text`, and `gui_window_clear` draw
  directly into the framebuffer, clipped to the window bounds for rectangles.
- `gui_draw` redraws the desktop background, all windows, focus borders, and
  the cursor.
- `gui_draw_demo`, `gui_draw_render`, `gui_demo_desktop`,
  `gui_demo_handle_input`, and the `gui_demo_*dirty*` helpers expose the current
  QEMU framebuffer demo surface.

Current input/event functions:

- `gui_hit_test` and `gui_window_contains` find the topmost window under a
  point.
- `gui_get_cursor`, `gui_set_cursor`, `gui_cursor_move`, and
  `gui_cursor_button` track cursor position and button state.
- `gui_drag_start`, `gui_drag_update`, `gui_drag_end`, and `gui_drag_active`
  implement window dragging.
- `gui_window_push_event` and `gui_window_pop_event` manage a fixed 32-event
  queue per window.
- `gui_dispatch_input` converts raw `input_event_t` keyboard and mouse motion
  into GUI events.

Current GUI event shape:

```c
typedef struct {
    uint32_t type;
    int32_t data1;
    int32_t data2;
} gui_event_t;
```

Event types are `GUI_EVENT_KEY_PRESS`, `GUI_EVENT_KEY_RELEASE`,
`GUI_EVENT_MOUSE_CLICK`, `GUI_EVENT_MOUSE_MOVE`, `GUI_EVENT_RESIZE`, and
`GUI_EVENT_CLOSE`. Resize and close are defined but not fully produced by the
window manager yet.

## Framebuffer And Input

The framebuffer path is real enough for QEMU demos:

- `drivers/gpu/virtio_gpu.c` provides a 640x480 virtio-gpu scanout.
- `drivers/fb/fb.c` provides scalar primitives and alpha-aware drawing.
- `kernel/font.c` draws the current 5x7 bitmap font.

Input is queued but still early:

- `drivers/input/input.c` owns a fixed raw input queue.
- `drivers/input/virtio_input.c` maps virtio keyboard and mouse events into
  `input_event_t`.
- UART key input is also mapped into `INPUT_EVENT_KEY_PRESS`.

## Userland And Apps

The tree already contains separate assembly app sources under `programs/apps/`
for `hello`, `loop`, `fault`, `shell`, `editor`, `monitor`, `win`, and
`panel`. The build embeds flat app blobs into the kernel and exposes them
through bootfs/VFS under `/kolibri/<name>`.

There is no `programs/libkarm`, `programs/libkarmdesk`, or `programs/libkarmgui`
yet. Current apps call syscalls directly from AArch64 assembly.

## Implemented GUI Syscalls

The current window syscall range starts at 70:

- `70 sys_window_create`
- `71 sys_window_destroy`
- `72 sys_window_draw_text`
- `73 sys_window_draw_rect`
- `74 sys_window_event`
- `75 sys_window_set_title`
- `76 sys_window_redraw`

These syscalls validate ownership by comparing the window owner pid with the
calling process pid. `sys_window_event` returns packed triples of
`type,data1,data2` and currently yields for a bounded number of scheduler turns
before returning `ERR_AGAIN`.

## Missing Or Experimental

- No stable userland C syscall wrapper library exists yet.
- No draw list, theme system, or widget library exists yet.
- Windows do not have separate backing buffers; drawing writes to the global
  framebuffer path.
- Redraw and expose handling are still demo-level.
- Close and resize events are defined but not routed from decorations.
- There are no titlebar buttons, minimize/maximize, or taskbar-owned focus
  controls yet.
- Text drawing is not clipped per glyph.
- Mouse event coordinates are absolute framebuffer coordinates, not a final
  normalized event ABI.
- There is no `sys_window_move`, `sys_window_resize`, `sys_window_get_bounds`,
  `sys_event_poll`, `sys_event_wait`, `sys_draw_line`, `sys_draw_bitmap`, theme,
  clipboard, or notification syscall.

## Desktop Ownership Decision

For the first desktop milestone, keep window management in the kernel-owned
GUI/compositor. The kernel owns framebuffer access, raw input, cursor state,
focus, hit testing, z-order, clipping, and per-process window ownership.

High-level desktop behavior should remain in userland. The panel, launcher,
taskbar, settings, app registry UI, draw lists, themes, and widgets should sit
above the syscall ABI in `programs/` libraries and apps. This keeps privileged
hardware behavior small while preventing the kernel from growing a widget
toolkit.
