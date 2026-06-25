// KolibriARM app: panel (C version, on libkarm + libkarmdesk)
//
// Draws a taskbar at the bottom of the screen with one launcher
// button per registered app and a running-apps row underneath.
//
// Click handling:
//   - Launcher button   -> sys_spawn("/kolibri/<name>")
//   - Running-apps slot -> if the window is minimised, restore it
//                          through gui_window_restore; otherwise raise
//                          it through SYS_WINDOW_FOCUS.
//
// The running-apps table is rebuilt every REFRESH_PERIOD yields by
// polling SYS_PROCLIST and resolving each pid through
// SYS_WINDOW_FOR_PID. For each non-self pid we also ask the kernel
// for the window's state bitmap (sys_window_state) so the slot
// can be drawn in the right colour: active for visible windows,
// greyed for minimised ones.
//
// The panel itself is filtered out by comparing pid against
// sys_getpid(), and SKIP_TASKBAR windows are filtered at the kernel
// by gui_window_for_pid, so the taskbar only ever lists normal
// app windows.

#include <stddef.h>
#include <stdint.h>

#include "libkarm/syscall.h"
#include "libkarm/string.h"
#include "libkarm/errno.h"
#include "libkarmdesk/gui.h"

// Geometry constants for a 640x480 virtio-gpu framebuffer.
#define SCREEN_W        640
#define SCREEN_H        480
#define PANEL_H          56
#define PANEL_Y         (SCREEN_H - PANEL_H)   // 424
#define TITLE_BAR_H      0                    // panel has no title bar
#define BTN_H            24
#define BTN_Y           (PANEL_Y + 4)         // 428
// 5 launchers must fit in 640 px with a 4 px left margin:
//   4 + 5*BTN_W + 4*BTN_GAP == 640 -> BTN_W=124, BTN_GAP=4
#define BTN_W           124
#define BTN_GAP           4
#define BTN_COUNT         5
#define BTN_ROW_W       (BTN_COUNT * BTN_W + (BTN_COUNT - 1) * BTN_GAP)
#define PANEL_CONTENT_W SCREEN_W
#define PANEL_CONTENT_H PANEL_H

#define RUN_ROW_Y        32
#define RUN_ROW_H       (PANEL_H - RUN_ROW_Y)
#define RUN_SLOT_W      150
#define RUN_SLOT_GAP     4
#define RUN_SLOT_COUNT    4
#define REFRESH_PERIOD 100

#define EVENT_CAP         8
#define PROCLIST_MAX      8

#define COLOR_BG          0xff202428U
#define COLOR_BORDER      0xff808080U
#define COLOR_TOPBAR      0xff506070U
#define COLOR_BTN_BG      0xff3a4658U
#define COLOR_BTN_HI      0xff5870a0U
#define COLOR_BTN_TXT     0xffe0e8f0U
#define COLOR_TITLE_TXT   0xff8090a0U
#define COLOR_RUN_BG      0xff182028U
#define COLOR_RUN_ACTIVE  0xff506880U
#define COLOR_RUN_MIN     0xff3a3a3cU
#define COLOR_RUN_TXT     0xffc0d0e0U

#define GUI_CURSOR_ARROW  0U
#define GUI_CURSOR_HAND   1U

#define NAME_CAP          16
#define LABEL_CAP         10   // matches panel_labels stride in the old asm

#define PATH_BUF_CAP      32

// gui_event_t is defined by libkarmdesk/gui.h; the layout is locked
// by _Static_assert(sizeof(gui_event_t) == 12).

// sys_proclist entry: pid(u32) state(u32) name[16] -> 24 bytes.
typedef struct {
    uint32_t pid;
    uint32_t state;
    char     name[NAME_CAP];
} proc_entry_t;

// One slot per running app. minimized is a local cache of the
// GUI_WINDOW_STATE_MINIMIZED bit returned by sys_window_state; we
// only re-query on refresh so a click that toggles minimise is
// reflected on the next poll, which is well below one frame.
typedef struct {
    uint32_t pid;
    uint32_t window_id;
    int      present;
    int      minimized;
    char     name[NAME_CAP];
} running_slot_t;

typedef struct {
    long           wid;
    uint32_t       panel_pid;
    int            hover;
    int            yield_counter;
    running_slot_t slots[RUN_SLOT_COUNT];
    proc_entry_t   procs[PROCLIST_MAX];
    char           button_labels[BTN_COUNT][LABEL_CAP];
    gui_event_t    events[EVENT_CAP];
} panel_state_t;

static void write_cstr(long fd, const char *s) {
    while (*s) {
        (void)kli_write((int)fd, s, 1);
        s++;
    }
}

static void copy_label(char *dst, size_t dst_size, const char *src) {
    size_t i = 0;
    while (i + 1 < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

// Convert an absolute framebuffer (x, y) to panel-local. The panel
// sits at x=0 so the conversion is just the y offset. Negative
// results (cursor above the panel) are clamped to -1 so the caller
// can early-out.
static int abs_to_panel_x(int32_t ax) {
    return (int)ax;
}

static int abs_to_panel_y(int32_t ay) {
    return (int)ay - PANEL_Y;
}

static int button_hit(int x) {
    if (x < 4) {
        return -1;
    }
    int adj = x - 4;
    int stride = BTN_W + BTN_GAP;
    int idx = adj / stride;
    int rem = adj - idx * stride;
    if (rem >= BTN_W || idx < 0 || idx >= BTN_COUNT) {
        return -1;
    }
    return idx;
}

static int slot_hit(int x) {
    if (x < 4) {
        return -1;
    }
    int adj = x - 4;
    int stride = RUN_SLOT_W + RUN_SLOT_GAP;
    int idx = adj / stride;
    int rem = adj - idx * stride;
    if (rem >= RUN_SLOT_W || idx < 0 || idx >= RUN_SLOT_COUNT) {
        return -1;
    }
    return idx;
}

static int name_text_width(const char *s) {
    int n = 0;
    while (s[n] != '\0' && n < NAME_CAP) {
        n++;
    }
    return n * 8;
}

static void draw_button(panel_state_t *p, int idx, int hovered) {
    if (idx < 0 || idx >= BTN_COUNT) {
        return;
    }
    int bx = 4 + idx * (BTN_W + BTN_GAP);
    long bg = hovered ? (long)COLOR_BTN_HI : (long)COLOR_BTN_BG;
    (void)gui_window_draw_rect(p->wid, bx, BTN_Y - PANEL_Y,
                               BTN_W, BTN_H, bg);
    (void)gui_window_draw_text(p->wid, bx + 8, BTN_Y - PANEL_Y + 6,
                               COLOR_BTN_TXT, p->button_labels[idx]);
}

static void draw_running_row(panel_state_t *p) {
    for (int i = 0; i < RUN_SLOT_COUNT; i++) {
        int sx = 4 + i * (RUN_SLOT_W + RUN_SLOT_GAP);
        long bg;
        if (p->slots[i].present == 0) {
            bg = (long)COLOR_RUN_BG;
        } else if (p->slots[i].minimized != 0) {
            bg = (long)COLOR_RUN_MIN;
        } else {
            bg = (long)COLOR_RUN_ACTIVE;
        }
        (void)gui_window_draw_rect(p->wid, sx, RUN_ROW_Y,
                                   RUN_SLOT_W, RUN_ROW_H, bg);
        if (p->slots[i].present != 0) {
            int text_w = name_text_width(p->slots[i].name);
            int pad = (RUN_SLOT_W - text_w) / 2;
            (void)gui_window_draw_text(p->wid, sx + pad, RUN_ROW_Y + 8,
                                       COLOR_RUN_TXT, p->slots[i].name);
        }
    }
}

static void redraw_all(panel_state_t *p) {
    // Background fill.
    (void)gui_window_draw_rect(p->wid, 0, 0, SCREEN_W, PANEL_H, COLOR_BG);
    // Top border.
    (void)gui_window_draw_rect(p->wid, 0, 0, SCREEN_W, 1, COLOR_TOPBAR);
    // Launcher buttons.
    for (int i = 0; i < BTN_COUNT; i++) {
        draw_button(p, i, p->hover == i ? 1 : 0);
    }
    // Separator above the running row.
    (void)gui_window_draw_rect(p->wid, 0, RUN_ROW_Y - 1,
                               SCREEN_W, 1, COLOR_TOPBAR);
    // Running apps.
    draw_running_row(p);
    // Flush the full panel content area.
    (void)gui_window_flush(p->wid, 0, 0, PANEL_CONTENT_W, PANEL_CONTENT_H);
}

// Build "/kolibri/<label>" into out. Returns the length written.
static size_t build_path(char *out, size_t out_size, const char *label) {
    static const char prefix[] = "/kolibri/";
    size_t pi = 0;
    while (pi + 1 < out_size && prefix[pi] != '\0') {
        out[pi] = prefix[pi];
        pi++;
    }
    size_t i = 0;
    while (pi + 1 < out_size && label[i] != '\0' && i < LABEL_CAP) {
        out[pi++] = label[i++];
    }
    if (pi < out_size) {
        out[pi] = '\0';
    }
    return pi;
}

static void launch_button(panel_state_t *p, int idx) {
    if (idx < 0 || idx >= BTN_COUNT) {
        return;
    }
    char path[PATH_BUF_CAP];
    size_t len = build_path(path, sizeof(path), p->button_labels[idx]);
    if (len == 0) {
        return;
    }

    /*
     * Refuse to launch another panel from the panel. Each spawned
     * panel has the same launcher buttons, so without this guard a
     * single click cascades into N nested panels until the process
     * table fills and every visible click starts returning -1.
     * The "panel" button is kept as a slot so the launcher row
     * layout stays fixed, but its click is just a no-op.
     */
    if (strcmp(p->button_labels[idx], "panel") == 0) {
        write_cstr(1, "panel: already running (no nested panels)\n");
        return;
    }

    write_cstr(1, "panel: launch ");
    write_cstr(1, p->button_labels[idx]);
    write_cstr(1, "\n");

    (void)kli_spawn(path, 0);
}

static void click_running_slot(panel_state_t *p, int idx) {
    if (idx < 0 || idx >= RUN_SLOT_COUNT) {
        return;
    }
    if (p->slots[idx].present == 0) {
        return;
    }
    if (p->slots[idx].minimized != 0) {
        (void)gui_window_restore((long)p->slots[idx].window_id);
    } else {
        (void)gui_window_focus((long)p->slots[idx].window_id);
    }
}

static void on_click(panel_state_t *p, int32_t abs_x, int32_t abs_y) {
    int x = abs_to_panel_x(abs_x);
    int y = abs_to_panel_y(abs_y);
    if (y < 0) {
        return;
    }
    // Button row first.
    if (y >= BTN_Y - PANEL_Y && y < BTN_Y - PANEL_Y + BTN_H) {
        int idx = button_hit(x);
        if (idx >= 0) {
            launch_button(p, idx);
        }
        return;
    }
    // Running-apps row.
    if (y >= RUN_ROW_Y && y < RUN_ROW_Y + RUN_ROW_H) {
        int idx = slot_hit(x);
        if (idx >= 0) {
            click_running_slot(p, idx);
        }
    }
}

static void on_move(panel_state_t *p, int32_t abs_x, int32_t abs_y) {
    int x = abs_to_panel_x(abs_x);
    int y = abs_to_panel_y(abs_y);
    int new_hover = -1;
    if (y >= BTN_Y - PANEL_Y && y < BTN_Y - PANEL_Y + BTN_H) {
        new_hover = button_hit(x);
    }
    if (new_hover == p->hover) {
        return;
    }
    int old_hover = p->hover;
    p->hover = new_hover;

    // The kernel walks the per-window cursor-shape regions every
    // refresh, so each launcher button slot has its own HAND region
    // (registered once in main) and the shape flips automatically
    // as the cursor crosses each boundary. No global cursor_set_shape
    // side effect, no risk of leaking HAND into the desktop above
    // the panel.

    // Repaint just the two affected buttons (old + new hover).
    if (old_hover >= 0 && old_hover < BTN_COUNT) {
        draw_button(p, old_hover, 0);
    }
    if (new_hover >= 0 && new_hover < BTN_COUNT) {
        draw_button(p, new_hover, 1);
    }
    (void)gui_window_flush(p->wid, 0, BTN_Y - PANEL_Y,
                           BTN_ROW_W, BTN_H);
}

static void refresh_running(panel_state_t *p) {
    // Clear the slot table first; entries we can't repopulate stay
    // empty until the next poll.
    for (int i = 0; i < RUN_SLOT_COUNT; i++) {
        p->slots[i].present = 0;
        p->slots[i].pid = 0;
        p->slots[i].window_id = 0;
        p->slots[i].minimized = 0;
        for (int j = 0; j < NAME_CAP; j++) {
            p->slots[i].name[j] = '\0';
        }
    }

    long n = kli_proclist(p->procs, PROCLIST_MAX);
    if (n <= 0) {
        return;
    }
    int slot_idx = 0;
    for (long i = 0; i < n && slot_idx < RUN_SLOT_COUNT; i++) {
        uint32_t pid = p->procs[i].pid;
        if (pid == 0 || pid == p->panel_pid) {
            continue;
        }
        long wid = gui_window_for_pid((long)pid, 0);
        if (wid < 0) {
            continue;
        }
        // Ask the kernel for the window's state bitmap so we can
        // render minimised slots in the greyed colour and skip the
        // FOCUS path on click (they go through RESTORE instead).
        uint32_t state = 0;
        long sr = gui_window_state(wid, &state);
        int minimized = (sr >= 0 &&
                         (state & GUI_WINDOW_STATE_MINIMIZED) != 0U) ? 1 : 0;
        p->slots[slot_idx].present = 1;
        p->slots[slot_idx].pid = pid;
        p->slots[slot_idx].window_id = (uint32_t)wid;
        p->slots[slot_idx].minimized = minimized;
        copy_label(p->slots[slot_idx].name, NAME_CAP,
                   p->procs[i].name);
        slot_idx++;
    }

    draw_running_row(p);
    // Flush only the running row so the launcher row stays put.
    (void)gui_window_flush(p->wid, 0, RUN_ROW_Y,
                           PANEL_CONTENT_W, RUN_ROW_H);
}

static void init_button_labels(panel_state_t *p) {
    /* Per-launcher labels are passed to copy_label directly as string
     * literals. parking them in a static const char*[] array would
     * put the pointer table in .data.rel.ro.local or .data, which
     * programs/apps/image.ld does NOT pull into the .user_image
     * section; the kernel's load stops at .user_image end, so
     * labels[0] would read as NULL from beyond image_size and the
     * strncpy walk would corrupt the stack. Inlining keeps every
     * literal in .user.image.rodata. */
    copy_label(p->button_labels[0], LABEL_CAP, "shell");
    copy_label(p->button_labels[1], LABEL_CAP, "editor");
    copy_label(p->button_labels[2], LABEL_CAP, "monitor");
    copy_label(p->button_labels[3], LABEL_CAP, "clock");
    copy_label(p->button_labels[4], LABEL_CAP, "panel");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* The panel state lives on the C stack instead of BSS. The
     * linker script in programs/apps/image.ld only collects
     * .user.image.{header,text,rodata} into the flat image; .bss
     * would land past image_size where the kernel has not mapped
     * any physical pages, so a static panel_state_t would fault on
     * the first read. The kernel gives every EL0 process a 4 KB
     * stack which comfortably fits the ~450 byte struct. */
    panel_state_t p;
    (void)p;
    p.wid = 0;
    p.panel_pid = 0;
    p.hover = -1;
    p.yield_counter = 0;
    for (int i = 0; i < RUN_SLOT_COUNT; i++) {
        p.slots[i].present = 0;
        p.slots[i].pid = 0;
        p.slots[i].window_id = 0;
        p.slots[i].minimized = 0;
        for (int j = 0; j < NAME_CAP; j++) {
            p.slots[i].name[j] = '\0';
        }
    }
    for (int i = 0; i < PROCLIST_MAX; i++) {
        p.procs[i].pid = 0;
        p.procs[i].state = 0;
        for (int j = 0; j < NAME_CAP; j++) {
            p.procs[i].name[j] = '\0';
        }
    }

    write_cstr(1, "panel: starting\n");
    init_button_labels(&p);

    p.wid = gui_window_create(0, PANEL_Y, SCREEN_W, PANEL_H,
                              COLOR_BG, COLOR_BORDER, "panel");
    if (p.wid < 0) {
        write_cstr(1, "panel: window create failed\n");
        // Park in a yield loop so the kernel debug console (k>) still
        // gets a chance to run and 'ps' / 'proclist' queries see a
        // process to point at, even when GUI setup failed.
        for (;;) {
            (void)kli_yield();
        }
    }

    /*
     * Install one HAND-shape cursor region per launcher button so
     * the kernel draws the hand cursor automatically as the mouse
     * crosses each button boundary. Coords are content-local, which
     * means (0, 0) is the top-left of the panel content area
     * (i.e. just below the title bar, if any). Using per-window
     * regions instead of the global SYS_CURSOR_SET_SHAPE means the
     * panel cannot leak a HAND cursor into the desktop above the
     * panel when the cursor leaves the launcher row.
     */
    for (int idx = 0; idx < BTN_COUNT; idx++) {
        int bx = 4 + idx * (BTN_W + BTN_GAP);
        int by = BTN_Y - PANEL_Y;
        (void)gui_cursor_register_region(p.wid, (long)idx, (long)bx,
                                          (long)by, (long)BTN_W,
                                          (long)BTN_H,
                                          (long)GUI_CURSOR_HAND);
    }

    long pid = kli_getpid();
    p.panel_pid = (pid > 0) ? (uint32_t)pid : 0;

    redraw_all(&p);
    refresh_running(&p);
    write_cstr(1, "panel: ready\n");

#ifdef PANEL_AUTO_TEST
    /*
     * Smoke test: launch every non-panel button once at boot so the
     * UART log shows whether each app survives main(), creates its
     * window, and stays alive. Disabled by default; enable by
     * passing -DPANEL_AUTO_TEST to the compiler.
     */
    write_cstr(1, "panel: auto-test launch every button\n");
    for (int idx = 0; idx < BTN_COUNT; idx++) {
        if (strcmp(p.button_labels[idx], "panel") == 0) {
            continue;
        }
        write_cstr(1, "panel: auto-test click ");
        write_cstr(1, p.button_labels[idx]);
        write_cstr(1, "\n");
        launch_button(&p, idx);
        (void)kli_yield();
    }
#endif

    for (;;) {
        long n = gui_window_event(p.wid, p.events, EVENT_CAP);
        if (n > 0) {
            int dirty = 0;
            for (long i = 0; i < n; i++) {
                if (p.events[i].type == GUI_EVENT_MOUSE_CLICK) {
                    on_click(&p, p.events[i].data1, p.events[i].data2);
                    dirty = 1;
                } else if (p.events[i].type == GUI_EVENT_MOUSE_MOVE) {
                    on_move(&p, p.events[i].data1, p.events[i].data2);
                    dirty = 1;
                }
            }
            (void)dirty;
        }

        p.yield_counter++;
        if (p.yield_counter >= REFRESH_PERIOD) {
            p.yield_counter = 0;
            refresh_running(&p);
        }

        (void)kli_yield();
    }
}
