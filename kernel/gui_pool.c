#include "kernel/gui_pool.h"

#include "fb/fb.h"
#include "kernel/gui_backing.h"
#include "kernel/gui_internal.h"
#include "kernel/kernel_compiler.h"

static int gui_str_eq(const char *a, const char *b) {
    uint32_t i = 0;
    if (a == 0 || b == 0) {
        return 0;
    }
    for (;;) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == '\0') {
            return 1;
        }
        i++;
    }
}

static uint32_t gui_window_effective_flags(const gui_window_t *window) {
    if (window == 0 || window->used == 0) {
        return 0;
    }

    return window->flags;
}

static int gui_rect_fits_framebuffer(const fb_t *fb, uint32_t x, uint32_t y,
                                     uint32_t w, uint32_t h) {
    if (fb == 0 || w == 0U || h == 0U ||
        x >= fb->width || y >= fb->height) {
        return 0;
    }

    return w <= fb->width - x && h <= fb->height - y;
}

static void gui_apply_builtin_policy(gui_window_t *window) {
    if (window == 0 || window->used == 0) {
        return;
    }

    if (gui_str_eq(window->title, "panel")) {
        window->flags |= GUI_WINDOW_DOCK | GUI_WINDOW_NO_FOCUS |
                         GUI_WINDOW_NO_DRAG | GUI_WINDOW_SKIP_TASKBAR;
    }
}

static KERNEL_ALWAYS_INLINE void gui_window_set_title(gui_window_t *window,
                                                      const char *title) {
    for (uint32_t i = 0; i < GUI_TITLE_LEN; i++) {
        window->title[i] = '\0';
    }
    if (title == 0) {
        return;
    }
    for (uint32_t i = 0; i + 1U < GUI_TITLE_LEN; i++) {
        if (title[i] == '\0') {
            break;
        }
        window->title[i] = title[i];
    }
}

const gui_window_t *gui_window_lookup(const gui_desktop_t *desktop,
                                      uint32_t window_id) {
    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS) {
        return 0;
    }
    if (desktop->windows[window_id].used == 0U) {
        return 0;
    }
    return &desktop->windows[window_id];
}

int gui_create_window(gui_desktop_t *desktop, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, uint32_t bg_color,
                      uint32_t border_color, uint32_t *window_id) {
    return gui_create_window_for_pid(desktop, GUI_NO_OWNER, x, y, w, h,
                                     bg_color, border_color, 0, window_id);
}

int gui_create_window_for_pid(gui_desktop_t *desktop, uint32_t owner_pid,
                              uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              uint32_t bg_color, uint32_t border_color,
                              const char *title, uint32_t *window_id) {
    if (desktop == 0 || desktop->fb == 0 || w < 2U || h < 2U ||
        gui_rect_fits_framebuffer(desktop->fb, x, y, w, h) == 0) {
        return -1;
    }

    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        gui_window_t *window = &desktop->windows[i];
        if (window->used != 0) {
            continue;
        }

        window->x = x;
        window->y = y;
        window->w = w;
        window->h = h;
        window->bg_color = bg_color;
        window->border_color = border_color;
        window->owner_pid = owner_pid;
        window->z = desktop->next_z++;
        window->flags = 0;
        window->title_h = 0;
        window->event_head = 0;
        window->event_tail = 0;
        window->event_count = 0;
        window->owner_drawn = 0;
        window->backing = 0;
        window->backing_capacity = 0;
        for (uint32_t j = 0; j < GUI_MAX_CURSOR_REGIONS; j++) {
            window->cursor_regions[j].x = 0;
            window->cursor_regions[j].y = 0;
            window->cursor_regions[j].w = 0;
            window->cursor_regions[j].h = 0;
            window->cursor_regions[j].shape = 0;
            window->cursor_regions[j].used = 0;
        }
        gui_window_set_title(window, title);
        window->used = 1;
        window->minimized = 0U;
        gui_apply_builtin_policy(window);

        if (desktop->focused_window_id == GUI_NO_WINDOW &&
            (window->flags & GUI_WINDOW_NO_FOCUS) == 0U) {
            desktop->focused_window_id = i;
        }
        if (window_id != 0) {
            *window_id = i;
        }
        gui_refresh_cursor_shape(desktop);
        gui_damage_add(desktop, (int32_t)x, (int32_t)y, (int32_t)w,
                       (int32_t)h);
        return 0;
    }

    return -1;
}

int gui_destroy_window(gui_desktop_t *desktop, uint32_t window_id) {
    gui_window_t *window;
    int32_t dx;
    int32_t dy;
    int32_t dw;
    int32_t dh;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    dx = (int32_t)window->x;
    dy = (int32_t)window->y;
    dw = (int32_t)window->w;
    dh = (int32_t)window->h;

    gui_window_free_backing(window);
    window->x = 0;
    window->y = 0;
    window->w = 0;
    window->h = 0;
    window->bg_color = 0;
    window->border_color = 0;
    window->owner_pid = GUI_NO_OWNER;
    window->z = 0;
    window->flags = 0;
    window->title_h = 0;
    window->event_head = 0;
    window->event_tail = 0;
    window->event_count = 0;
    window->used = 0;
    window->owner_drawn = 0;
    for (uint32_t j = 0; j < GUI_MAX_CURSOR_REGIONS; j++) {
        window->cursor_regions[j].x = 0;
        window->cursor_regions[j].y = 0;
        window->cursor_regions[j].w = 0;
        window->cursor_regions[j].h = 0;
        window->cursor_regions[j].shape = 0;
        window->cursor_regions[j].used = 0;
    }
    gui_window_set_title(window, 0);

    if (desktop->focused_window_id == window_id) {
        desktop->focused_window_id = GUI_NO_WINDOW;
        (void)gui_focus_window_ensure(desktop);
    }
    gui_refresh_cursor_shape(desktop);
    gui_damage_add(desktop, dx, dy, dw, dh);
    return 0;
}

void gui_destroy_windows_for_pid(gui_desktop_t *desktop, uint32_t pid) {
    if (desktop == 0 || pid == GUI_NO_OWNER) {
        return;
    }

    for (uint32_t i = GUI_MAX_WINDOWS; i-- > 0;) {
        if (desktop->windows[i].used != 0 &&
            desktop->windows[i].owner_pid == pid) {
            (void)gui_destroy_window(desktop, i);
        }
    }
}

int gui_window_set_title_internal(gui_desktop_t *desktop, uint32_t window_id,
                                  const char *title) {
    gui_window_t *window;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    gui_window_set_title(window, title);
    gui_apply_builtin_policy(window);
    gui_refresh_cursor_shape(desktop);
    if (window->title_h > 0U) {
        gui_damage_add(desktop, (int32_t)window->x, (int32_t)window->y,
                       (int32_t)window->w, (int32_t)window->title_h);
    }
    return 0;
}

int gui_window_set_title_bar_internal(gui_desktop_t *desktop, uint32_t window_id,
                                      uint32_t title_h) {
    gui_window_t *window;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    if (title_h >= window->h) {
        return -1;
    }

    window->title_h = title_h;
    gui_refresh_cursor_shape(desktop);
    gui_damage_add(desktop, (int32_t)window->x, (int32_t)window->y,
                   (int32_t)window->w, (int32_t)window->h);
    return 0;
}

int gui_window_set_flags_internal(gui_desktop_t *desktop, uint32_t window_id,
                                  uint32_t flags) {
    gui_window_t *window;
    uint32_t old_flags;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    old_flags = window->flags;
    window->flags = flags;
    if ((flags & GUI_WINDOW_NO_FOCUS) != 0U &&
        desktop->focused_window_id == window_id) {
        desktop->focused_window_id = GUI_NO_WINDOW;
        (void)gui_focus_window_ensure(desktop);
    }
    if (((old_flags ^ flags) & (GUI_WINDOW_NO_FOCUS | GUI_WINDOW_NO_DRAG |
                                GUI_WINDOW_DOCK)) != 0U) {
        gui_refresh_cursor_shape(desktop);
        gui_damage_add(desktop, (int32_t)window->x, (int32_t)window->y,
                       (int32_t)window->w, (int32_t)window->h);
    }
    return 0;
}

int gui_move_window(gui_desktop_t *desktop, uint32_t window_id, uint32_t x,
                    uint32_t y) {
    gui_window_t *window;
    int32_t old_x;
    int32_t old_y;
    int32_t old_w;
    int32_t old_h;
    int32_t ux0;
    int32_t uy0;
    int32_t ux1;
    int32_t uy1;

    if (desktop == 0 || desktop->fb == 0 || window_id >= GUI_MAX_WINDOWS ||
        x >= desktop->fb->width || y >= desktop->fb->height ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    old_x = (int32_t)window->x;
    old_y = (int32_t)window->y;
    old_w = (int32_t)window->w;
    old_h = (int32_t)window->h;
    window->x = x;
    window->y = y;
    gui_refresh_cursor_shape(desktop);

    ux0 = old_x < (int32_t)x ? old_x : (int32_t)x;
    uy0 = old_y < (int32_t)y ? old_y : (int32_t)y;
    ux1 = old_x + old_w > (int32_t)x + old_w ? old_x + old_w
                                              : (int32_t)x + old_w;
    uy1 = old_y + old_h > (int32_t)y + old_h ? old_y + old_h
                                              : (int32_t)y + old_h;
    gui_damage_add(desktop, ux0, uy0, ux1 - ux0, uy1 - uy0);
    return 0;
}

int gui_resize_window(gui_desktop_t *desktop, uint32_t window_id, uint32_t x,
                      uint32_t y, uint32_t w, uint32_t h) {
    gui_window_t *window;
    int32_t old_x;
    int32_t old_y;
    int32_t new_x;
    int32_t new_y;
    int32_t old_w;
    int32_t old_h;
    int32_t ux0;
    int32_t uy0;
    int32_t ux1;
    int32_t uy1;
    int32_t size_changed;

    if (desktop == 0 || desktop->fb == 0 || window_id >= GUI_MAX_WINDOWS ||
        w < 2U || h < 2U ||
        gui_rect_fits_framebuffer(desktop->fb, x, y, w, h) == 0 ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    old_x = (int32_t)window->x;
    old_y = (int32_t)window->y;
    old_w = (int32_t)window->w;
    old_h = (int32_t)window->h;
    new_x = (int32_t)x;
    new_y = (int32_t)y;
    size_changed = (old_w != (int32_t)w || old_h != (int32_t)h) ? 1 : 0;

    window->x = x;
    window->y = y;
    window->w = w;
    window->h = h;
    gui_refresh_cursor_shape(desktop);

    if (size_changed != 0) {
        if (gui_window_reset_backing(window) != 0) {
            window->x = (uint32_t)old_x;
            window->y = (uint32_t)old_y;
            window->w = (uint32_t)old_w;
            window->h = (uint32_t)old_h;
            return -1;
        }
        (void)gui_window_push_event(window, GUI_EVENT_RESIZE,
                                    (int32_t)w, (int32_t)h);
    }

    ux0 = old_x < new_x ? old_x : new_x;
    uy0 = old_y < new_y ? old_y : new_y;
    ux1 = old_x + old_w > new_x + (int32_t)w ? old_x + old_w
                                              : new_x + (int32_t)w;
    uy1 = old_y + old_h > new_y + (int32_t)h ? old_y + old_h
                                              : new_y + (int32_t)h;
    gui_damage_add(desktop, ux0, uy0, ux1 - ux0, uy1 - uy0);
    return 0;
}

int gui_window_get_bounds(const gui_window_t *window, uint32_t *out_x,
                          uint32_t *out_y, uint32_t *out_w, uint32_t *out_h) {
    if (window == 0 || window->used == 0) {
        return -1;
    }
    if (out_x != 0) {
        *out_x = window->x;
    }
    if (out_y != 0) {
        *out_y = window->y;
    }
    if (out_w != 0) {
        *out_w = window->w;
    }
    if (out_h != 0) {
        *out_h = window->h;
    }
    return 0;
}

int gui_window_minimize(gui_desktop_t *desktop, uint32_t window_id) {
    gui_window_t *window;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS) {
        return -1;
    }
    window = &desktop->windows[window_id];
    if (window->used == 0 || window->minimized != 0U) {
        return -1;
    }
    window->minimized = 1U;
    (void)gui_window_push_event(window, GUI_EVENT_MINIMIZE, 0, 0);
    gui_request_redraw();
    return 0;
}

int gui_window_restore(gui_desktop_t *desktop, uint32_t window_id) {
    gui_window_t *window;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS) {
        return -1;
    }
    window = &desktop->windows[window_id];
    if (window->used == 0 || window->minimized == 0U) {
        return -1;
    }
    window->minimized = 0U;
    if ((gui_window_effective_flags(window) & GUI_WINDOW_NO_FOCUS) == 0U) {
        desktop->focused_window_id = window_id;
    }
    window->z = desktop->next_z++;
    (void)gui_window_push_event(window, GUI_EVENT_MAXIMIZE, 0, 0);
    gui_request_redraw();
    return 0;
}

int gui_focus_window(gui_desktop_t *desktop, uint32_t window_id) {
    uint32_t prev;
    gui_window_t *window;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    if ((gui_window_effective_flags(window) & GUI_WINDOW_NO_FOCUS) != 0U) {
        gui_damage_add(desktop, (int32_t)window->x, (int32_t)window->y,
                       (int32_t)window->w, (int32_t)window->h);
        return 0;
    }

    prev = desktop->focused_window_id;
    desktop->focused_window_id = window_id;
    window->z = desktop->next_z++;
    gui_refresh_cursor_shape(desktop);

    if (prev != GUI_NO_WINDOW && prev < GUI_MAX_WINDOWS &&
        desktop->windows[prev].used != 0) {
        gui_window_t *old = &desktop->windows[prev];
        gui_damage_add(desktop, (int32_t)old->x, (int32_t)old->y,
                       (int32_t)old->w, (int32_t)old->h);
    }
    gui_damage_add(desktop, (int32_t)window->x, (int32_t)window->y,
                   (int32_t)window->w, (int32_t)window->h);
    return 0;
}

int gui_focus_window_ensure(gui_desktop_t *desktop) {
    if (desktop == 0) {
        return -1;
    }
    if (desktop->focused_window_id != GUI_NO_WINDOW &&
        desktop->focused_window_id < GUI_MAX_WINDOWS &&
        desktop->windows[desktop->focused_window_id].used != 0 &&
        (desktop->windows[desktop->focused_window_id].flags &
         GUI_WINDOW_NO_FOCUS) == 0U) {
        return 0;
    }

    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (desktop->windows[i].used != 0 &&
            (desktop->windows[i].flags & GUI_WINDOW_NO_FOCUS) == 0U) {
            desktop->focused_window_id = i;
            return 0;
        }
    }

    desktop->focused_window_id = GUI_NO_WINDOW;
    return -1;
}

uint32_t gui_window_for_pid(gui_desktop_t *desktop, uint32_t owner_pid,
                            uint32_t index) {
    uint32_t found = 0;

    if (desktop == 0 || owner_pid == GUI_NO_OWNER) {
        return GUI_NO_WINDOW;
    }

    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        const gui_window_t *window = &desktop->windows[i];
        if (window->used == 0 || window->owner_pid != owner_pid ||
            (window->flags & GUI_WINDOW_SKIP_TASKBAR) != 0U) {
            continue;
        }
        if (found == index) {
            return i;
        }
        found++;
    }

    return GUI_NO_WINDOW;
}
