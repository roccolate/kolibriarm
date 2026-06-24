// KolibriARM app: editor (C version)
//
// Minimal GUI editor for /tmp/note (or argv[1] if the shell passed a
// path). It owns a window, displays the first screenful of the file,
// appends printable key presses to the buffer, supports backspace
// and newline, saves with Ctrl-S, and exits with Ctrl-Q or the
// title-bar close button.
//
// This is the third app migrated to programs/libkarm. The full I/O
// path (open, read, write, close, seek) goes through libkarm's typed
// wrappers in <libkarm/syscall.h>. argv comes in via the C main
// signature (libkarm's crt0 forwards x0/x1 from sys_spawn_argv).
// Window syscalls still go through libkarm's raw __syscallN
// trampolines — programs/libkarmdesk is not built yet.

#include <stddef.h>
#include <stdint.h>

#include "libkarm/syscall.h"
#include "libkarm/string.h"
#include "libkarm/errno.h"
#include "kernel/syscall_numbers.h"

#define O_RDONLY        0
#define O_RDWR          2
#define WIN_X          96
#define WIN_Y          72
#define WIN_W         420
#define WIN_H         260
#define TITLE_BAR_H    12
#define FILE_CAP      512
#define RENDER_CAP    128
#define EVENT_CAP       8
#define PATH_CAP       16

#define COLOR_BG        0xff202830U
#define COLOR_BORDER    0xff808080U
#define COLOR_STATUS    0xffc0d0e0U
#define COLOR_TEXT      0xffe0e8f0U

// gui_event_t must match kernel/gui.h: type(u32) data1(i32) data2(i32).
typedef struct {
    uint32_t type;
    int32_t  data1;
    int32_t  data2;
} gui_event_t;

#define GUI_EVENT_KEY_PRESS  1U
#define GUI_EVENT_CLOSE      6U

// Window/compositor syscalls via libkarm's raw trampolines.
extern long __syscall1(long n, long a0);
extern long __syscall3(long n, long a0, long a1, long a2);
extern long __syscall6(long n, long a0, long a1, long a2,
                       long a3, long a4, long a5);

static long win_create(long x, long y, long w, long h,
                       long bg, long border) {
    return __syscall6(SYS_WINDOW_CREATE, x, y, w, h, bg, border);
}

static long win_destroy(long wid) {
    return __syscall1(SYS_WINDOW_DESTROY, wid);
}

static long win_set_title(long wid, const char *title, long h) {
    return __syscall3(SYS_WINDOW_SET_TITLE, wid, (long)(uintptr_t)title, h);
}

static long win_draw_rect(long wid, long x, long y, long w, long h,
                          long color) {
    return __syscall6(SYS_WINDOW_DRAW_RECT, wid, x, y, w, h, color);
}

static long win_draw_text(long wid, long x, long y, long color,
                          const char *str) {
    return __syscall6(SYS_WINDOW_DRAW_TEXT, wid, x, y, color,
                      (long)(uintptr_t)str, 0);
}

static long win_flush(long wid, long x, long y, long w, long h) {
    return __syscall6(SYS_WINDOW_FLUSH, wid, x, y, w, h, 0);
}

static long win_event(long wid, gui_event_t *buf, long cap) {
    return __syscall3(SYS_WINDOW_EVENT, wid, (long)(uintptr_t)buf, cap);
}

static void write_cstr(long fd, const char *s) {
    while (*s) {
        (void)kli_write((int)fd, s, 1);
        s++;
    }
}

// Resolve the editor's file path. If argv[1] exists, use it; otherwise
// fall back to "/tmp/note". Writes into `out` (PATH_CAP bytes).
static void resolve_path(int argc, char **argv, char *out) {
    if (argc >= 2 && argv[1] != 0) {
        size_t i = 0;
        while (i + 1 < (size_t)PATH_CAP && argv[1][i] != '\0') {
            out[i] = argv[1][i];
            i++;
        }
        out[i] = '\0';
        return;
    }
    // "/tmp/note" is 9 chars + NUL, well under PATH_CAP.
    const char *def = "/tmp/note";
    size_t i = 0;
    while (def[i] != '\0') {
        out[i] = def[i];
        i++;
    }
    out[i] = '\0';
}

// Best-effort load of the resolved path into `buf`, returning the
// number of bytes actually read.
static size_t load_note(const char *path, char *buf, size_t cap) {
    long fd = kli_open(path, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    long n = kli_read((int)fd, buf, cap - 1);
    (void)kli_close((int)fd);
    if (n < 0) {
        return 0;
    }
    buf[n] = '\0';
    return (size_t)n;
}

// Save the buffer to disk. Errors are silent — the editor keeps the
// in-memory copy and the user can fix the path and try again.
static void save_note(const char *path, const char *buf, size_t len) {
    long fd = kli_open(path, O_RDWR);
    if (fd < 0) {
        return;
    }
    (void)kli_seek((int)fd, 0, 0);
    (void)kli_write((int)fd, buf, len);
    (void)kli_close((int)fd);
}

static void redraw(long wid, const char *file_buf, size_t file_len,
                   int argc, char **argv, char *render) {
    // Clear content area.
    (void)win_draw_rect(wid, 1, 0, WIN_W - 2, WIN_H - TITLE_BAR_H - 2,
                        COLOR_BG);
    (void)win_draw_text(wid, 12, 8, COLOR_STATUS,
                        "CTRL S SAVE CTRL Q CLOSE");

    if (argc > 1) {
        // Build "ARGV: <tok0> <tok1> ..." into render and draw.
        size_t i = 0;
        const char *prefix = "ARGV: ";
        while (prefix[i] != '\0' && i + 1 < RENDER_CAP) {
            render[i] = prefix[i];
            i++;
        }
        for (int a = 0; a < argc && i + 1 < RENDER_CAP; a++) {
            if (i + 1 < RENDER_CAP) {
                render[i++] = ' ';
            }
            const char *s = (a < argc && argv[a] != 0) ? argv[a] : "";
            for (size_t k = 0; s[k] != '\0' && i + 1 < RENDER_CAP; k++) {
                render[i++] = s[k];
            }
        }
        render[i] = '\0';
        (void)win_draw_text(wid, 12, 22, COLOR_STATUS, render);
    }

    // Copy visible prefix of the file buffer into render and draw.
    size_t i = 0;
    while (i + 1 < RENDER_CAP && i < file_len) {
        render[i] = file_buf[i];
        i++;
    }
    render[i] = '\0';
    (void)win_draw_text(wid, 12, 28, COLOR_TEXT, render);

    (void)win_flush(wid, 0, 0, WIN_W, WIN_H - TITLE_BAR_H);
}

static void print_argv(int argc, char **argv) {
    if (argc <= 0) {
        return;
    }
    write_cstr(1, "editor: argv=");
    for (int i = 0; i < argc; i++) {
        if (i > 0) {
            write_cstr(1, " ");
        }
        if (argv[i] != 0) {
            write_cstr(1, argv[i]);
        }
    }
    write_cstr(1, "\n");
}

int main(int argc, char **argv) {
    char path[PATH_CAP];
    char file[FILE_CAP];
    size_t file_len = 0;
    char render[RENDER_CAP];
    gui_event_t events[EVENT_CAP];

    write_cstr(1, "editor: starting\n");
    print_argv(argc, argv);

    resolve_path(argc, argv, path);
    file_len = load_note(path, file, sizeof(file));

    long wid = win_create(WIN_X, WIN_Y, WIN_W, WIN_H,
                          COLOR_BG, COLOR_BORDER);
    if (wid < 0) {
        write_cstr(1, "editor: window create failed\n");
        return 1;
    }
    (void)win_set_title(wid, "editor", TITLE_BAR_H);

    redraw(wid, file, file_len, argc, argv, render);

    for (;;) {
        long n = win_event(wid, events, EVENT_CAP);
        if (n > 0) {
            for (long i = 0; i < n; i++) {
                if (events[i].type == GUI_EVENT_CLOSE) {
                    (void)win_destroy(wid);
                    return 0;
                }
                if (events[i].type != GUI_EVENT_KEY_PRESS) {
                    continue;
                }
                int key = events[i].data1;
                if (key == 17) {                  // Ctrl-Q
                    (void)win_destroy(wid);
                    return 0;
                }
                if (key == 19) {                  // Ctrl-S
                    save_note(path, file, file_len);
                } else if (key == 8 || key == 127) {  // backspace
                    if (file_len > 0) {
                        file_len--;
                        file[file_len] = '\0';
                    }
                } else if (key == 13 || key == 10) {  // newline
                    if (file_len + 1 < FILE_CAP) {
                        file[file_len++] = '\n';
                        file[file_len]   = '\0';
                    }
                } else if (key >= 32 && key <= 126) {
                    if (file_len + 1 < FILE_CAP) {
                        file[file_len++] = (char)key;
                        file[file_len]   = '\0';
                    }
                }
            }
            redraw(wid, file, file_len, argc, argv, render);
        } else {
            (void)kli_yield();
        }
    }
}
