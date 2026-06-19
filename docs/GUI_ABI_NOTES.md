# GUI ABI Notes

These notes document the current ABI and the intended next cleanup steps. They
are not a promise of long-term binary compatibility yet.

## Live Window Syscalls

The live window range is `70..79`. Do not reuse `60..69`; `60` and `61` are
already used by fixed-message IPC.

| # | Name | Arguments | Return |
|---|------|-----------|--------|
| 70 | `sys_window_create` | `x0=x, x1=y, x2=w, x3=h, x4=bg, x5=border, x6=title_ptr` | window id / error |
| 71 | `sys_window_destroy` | `x0=window_id` | 0 / error |
| 72 | `sys_window_draw_text` | `x0=window_id, x1=x, x2=y, x3=color, x4=str_ptr` | 0 / error |
| 73 | `sys_window_draw_rect` | `x0=window_id, x1=x, x2=y, x3=w, x4=h, x5=color` | 0 / error |
| 74 | `sys_window_event` | `x0=window_id, x1=buf_ptr, x2=event_capacity` | event count / error |
| 75 | `sys_window_set_title` | `x0=window_id, x1=title_ptr, x2=title_h` | 0 / error |
| 76 | `sys_window_redraw` | `x0=window_id` | 0 / error |
| 77 | `sys_window_focus` | `x0=window_id` | 0 / error |
| 78 | `sys_window_for_pid` | `x0=owner_pid, x1=index` | window id / `ERR_NOENT` |
| 79 | `sys_cursor_set_shape` | `x0=shape` | 0 / error |

All draw / destroy / set-title window syscalls require the caller to own the
target window. The kernel checks `gui_window_t.owner_pid` against the current
process pid. The two new syscalls `sys_window_focus` and `sys_window_for_pid`
are deliberately callable from any pid so the desktop taskbar (which does not
own app windows) can raise and enumerate them. `sys_cursor_set_shape` lets
EL0-drawn controls request `0=arrow` or `1=hand`.

## Event Buffer

`sys_window_event` writes one or more fixed 12-byte records:

```c
typedef struct {
    uint32_t type;
    int32_t data1;
    int32_t data2;
} gui_event_t;
```

Current event types:

| Type | Name | data1 | data2 |
|------|------|-------|-------|
| 1 | `GUI_EVENT_KEY_PRESS` | key value | 0 |
| 2 | `GUI_EVENT_KEY_RELEASE` | key value | 0 |
| 3 | `GUI_EVENT_MOUSE_CLICK` | absolute x | absolute y |
| 4 | `GUI_EVENT_MOUSE_MOVE` | absolute x | absolute y |
| 5 | `GUI_EVENT_RESIZE` | width | height |
| 6 | `GUI_EVENT_CLOSE` | 0 | 0 |

Resize is still an ABI placeholder. The compositor does not yet
produce them from decorations.

## Current Limitations

- `sys_window_create` exposes colors directly instead of using theme tokens.
- There is no separate `sys_window_flush`; `sys_window_redraw` only marks the
  demo desktop dirty.
- `sys_window_event` is a bounded wait, not a clean poll/wait split.
- Event records do not include a timestamp, modifiers, mouse button mask, or
  target window id.
- Mouse coordinates are absolute. A future library wrapper can normalize them
  to window-local coordinates for apps.
- Drawing syscalls operate immediately and directly; there is no draw-list
  submission or per-window back buffer yet.
- Cursor shape hints are global and minimal; there is no per-window cursor
  region registry yet.
- The optional `title_h` argument of `sys_window_set_title` is silently
  ignored (`title_h == 0`) by apps that pre-date the title bar feature; the
  kernel still validates `title_h >= window->h`.

## Missing ABI Surface

Window management still needs:

- `sys_window_get_bounds`
- `sys_window_set_bounds` or separate move/resize calls
- `sys_window_show` and `sys_window_hide`
- `sys_window_flush` with an explicit dirty rectangle
- close/minimize/maximize decoration events
- process-exit cleanup or orphan-window policy

Drawing still needs:

- `sys_draw_begin` and `sys_draw_end`
- `sys_draw_clear`
- `sys_draw_rect_outline`
- `sys_draw_line`
- `sys_draw_bitmap`
- `sys_draw_get_text_metrics`

Events still need:

- `sys_event_poll`
- `sys_event_wait`
- `sys_event_peek`
- timer events or a separate timer syscall family

Desktop integration can wait until the core window ABI is less fluid:

- desktop info
- theme get/set
- clipboard get/set
- notifications

## Ownership Model

Keep the compositor and low-level window state in the kernel for now. Userland
apps and libraries should see handles, events, and drawing commands only.

The desktop process should eventually own the taskbar, launcher, menu, themes,
and high-level app lifecycle. Widgets belong in `libkarmgui`, not in the
kernel.
