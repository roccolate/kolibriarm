// KolibriARM app: editor (C version, on libkarm + libkarmdesk)
//
// Minimal GUI editor for /tmp/note (or argv[1] if the shell passed a
// path). It owns a window, displays the first screenful of the file,
// appends printable key presses to the buffer, supports backspace
// and newline, saves with Ctrl-S, and exits with Ctrl-Q or the
// title-bar close button.
//
// Non-window syscalls (open/read/write/close/seek/yield/exit) go
// through libkarm's typed wrappers. Window syscalls go through
// libkarmdesk's typed gui_* wrappers.

#include <stddef.h>
#include <stdint.h>

#include "libkarm/syscall.h"
#include "libkarm/string.h"
#include "libkarm/errno.h"
#include "libkarmdesk/gui.h"

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
    const char *def = "/tmp/note";
    size_t i = 0;
    while (def[i] != '\0') {
        out[i] = def[i];
        i++;
    }
    out[i] = '\0';
}

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
    (void)gui_window_draw_rect(wid, 1, 0, WIN_W - 2,
                               WIN_H - TITLE_BAR_H - 2, COLOR_BG);
    (void)gui_window_draw_text(wid, 12, 8, COLOR_STATUS,
                               "CTRL S SAVE CTRL Q CLOSE");

    if (argc > 1) {
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
        (void)gui_window_draw_text(wid, 12, 22, COLOR_STATUS, render);
    }

    size_t i = 0;
    while (i + 1 < RENDER_CAP && i < file_len) {
        render[i] = file_buf[i];
        i++;
    }
    render[i] = '\0';
    (void)gui_window_draw_text(wid, 12, 28, COLOR_TEXT, render);

    (void)gui_window_flush(wid, 0, 0, WIN_W, WIN_H - TITLE_BAR_H);
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

    long wid = gui_window_create(WIN_X, WIN_Y, WIN_W, WIN_H,
                                 COLOR_BG, COLOR_BORDER, "editor");
    if (wid < 0) {
        write_cstr(1, "editor: window create failed\n");
        return 1;
    }
    (void)gui_window_set_title(wid, "editor", TITLE_BAR_H);

    redraw(wid, file, file_len, argc, argv, render);

    for (;;) {
        long n = gui_window_event(wid, events, EVENT_CAP);
        if (n > 0) {
            for (long i = 0; i < n; i++) {
                if (events[i].type == GUI_EVENT_CLOSE) {
                    (void)gui_window_destroy(wid);
                    return 0;
                }
                if (events[i].type != GUI_EVENT_KEY_PRESS) {
                    continue;
                }
                int key = events[i].data1;
                if (key == 17) {
                    (void)gui_window_destroy(wid);
                    return 0;
                }
                if (key == 19) {
                    save_note(path, file, file_len);
                } else if (key == 8 || key == 127) {
                    if (file_len > 0) {
                        file_len--;
                        file[file_len] = '\0';
                    }
                } else if (key == 13 || key == 10) {
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
