// KolibriARM app: editor (C version, on libkarm + libkarmdesk)
//
// Minimal GUI editor for /tmp/note (or argv[1] if the shell passed a
// path). It owns a window, displays the line the caret is on (capped
// at RENDER_COLS chars), supports printable keys, backspace, newline,
// Left/Right/Up/Down caret movement, and a visible caret block. Saves
// with Ctrl-S, exits with Ctrl-Q or the title-bar close button.
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
#define TITLE_BAR_H    16
#define FILE_CAP      512
#define RENDER_COLS   128
#define EVENT_CAP       8
#define PATH_CAP       16

#define COLOR_BG        0xff202830U
#define COLOR_BORDER    0xff808080U
#define COLOR_STATUS    0xffc0d0e0U
#define COLOR_TEXT      0xffe0e8f0U
#define COLOR_CARET     0xffe0e8f0U

static void write_cstr(long fd, const char *s) {
    while (*s) {
        (void)kli_write((int)fd, s, 1);
        s++;
    }
}

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

// Editor state. caret is the offset in file[0..file_len] where the
// next character will be inserted or deleted. Backspace at caret
// removes file[caret-1]; printable at caret shifts the tail right.
// Left/Right move within file_len; Up/Down move to the same column
// on the previous/next line.
typedef struct {
    char   file[FILE_CAP];
    size_t file_len;
    size_t caret;
} editor_state_t;

static void editor_init(editor_state_t *e) {
    for (size_t i = 0; i < FILE_CAP; i++) {
        e->file[i] = '\0';
    }
    e->file_len = 0;
    e->caret = 0;
}

static void editor_load(editor_state_t *e, const char *path) {
    e->file_len = load_note(path, e->file, sizeof(e->file));
    if (e->caret > e->file_len) {
        e->caret = e->file_len;
    }
}

static void editor_save(const editor_state_t *e, const char *path) {
    save_note(path, e->file, e->file_len);
}

// Find the start of the line containing `caret`. Lines are split by
// '\n'; the first line starts at offset 0.
static size_t editor_line_start(const editor_state_t *e, size_t caret) {
    if (caret > e->file_len) {
        caret = e->file_len;
    }
    size_t start = 0;
    for (size_t i = 0; i < caret; i++) {
        if (e->file[i] == '\n') {
            start = i + 1;
        }
    }
    return start;
}

// Find the end of the line containing `caret` — the index of the
// terminating '\n' or one past the last char.
static size_t editor_line_end(const editor_state_t *e, size_t caret) {
    if (caret > e->file_len) {
        caret = e->file_len;
    }
    for (size_t i = caret; i < e->file_len; i++) {
        if (e->file[i] == '\n') {
            return i;
        }
    }
    return e->file_len;
}

// Move caret one character to the left within its line.
static void editor_left(editor_state_t *e) {
    size_t line_start = editor_line_start(e, e->caret);
    if (e->caret > line_start) {
        e->caret--;
    }
}

// Move caret one character to the right within its line. The
// terminating '\n' of a line is part of the line, so caret stops on
// the '\n' instead of jumping to the next line.
static void editor_right(editor_state_t *e) {
    size_t line_end = editor_line_end(e, e->caret);
    if (e->caret < line_end) {
        e->caret++;
    }
}

// Move caret to the previous line at the same column, snapping to the
// shorter line if the previous one is shorter.
static void editor_up(editor_state_t *e) {
    size_t line_start = editor_line_start(e, e->caret);
    if (line_start == 0) {
        return;
    }
    size_t col = e->caret - line_start;
    size_t prev_end = line_start - 1;             // index of '\n'
    size_t prev_start = editor_line_start(e, prev_end);
    size_t prev_len = prev_end - prev_start;
    e->caret = prev_start + (col < prev_len ? col : prev_len);
}

// Move caret to the next line at the same column, snapping to the
// shorter line if the next one is shorter.
static void editor_down(editor_state_t *e) {
    size_t line_end = editor_line_end(e, e->caret);
    if (line_end >= e->file_len) {
        return;
    }
    size_t line_start = editor_line_start(e, e->caret);
    size_t col = e->caret - line_start;
    size_t next_start = line_end + 1;
    size_t next_end = editor_line_end(e, next_start);
    size_t next_len = next_end - next_start;
    e->caret = next_start + (col < next_len ? col : next_len);
}

// Insert `c` at the caret. Returns 0 on success, -1 if the buffer is
// full. The tail is shifted right by one.
static int editor_insert(editor_state_t *e, char c) {
    if (e->file_len + 1 >= FILE_CAP) {
        return -1;
    }
    for (size_t i = e->file_len; i > e->caret; i--) {
        e->file[i] = e->file[i - 1];
    }
    e->file[e->caret] = c;
    e->file_len++;
    e->caret++;
    e->file[e->file_len] = '\0';
    return 0;
}

// Delete the character before the caret. Returns 0 on success, -1 if
// the caret is at the start of the file.
static int editor_backspace(editor_state_t *e) {
    if (e->caret == 0) {
        return -1;
    }
    e->caret--;
    for (size_t i = e->caret; i + 1 < e->file_len; i++) {
        e->file[i] = e->file[i + 1];
    }
    e->file_len--;
    e->file[e->file_len] = '\0';
    return 0;
}

// Render the line containing the caret into out and draw the caret
// block at (12 + col*8, 28). Returns the col where the caret sits so
// the caller can sanity-check.
static size_t editor_draw_line(long wid, const editor_state_t *e,
                               char *out, size_t out_size) {
    size_t line_start = editor_line_start(e, e->caret);
    size_t line_end = editor_line_end(e, e->caret);
    size_t col = e->caret - line_start;

    size_t i = 0;
    for (size_t k = line_start; k < line_end && i + 1 < out_size; k++) {
        out[i++] = e->file[k];
    }
    out[i] = '\0';

    (void)gui_window_draw_text(wid, 12, 28, COLOR_TEXT, out);
    // 2x8 caret block. Two pixels wide is enough to read on the 8x8
    // font and tall enough to span the glyph.
    (void)gui_window_draw_rect(wid, 12 + (long)col * 8, 28, 2, 8,
                               COLOR_CARET);
    return col;
}

static void redraw(long wid, const editor_state_t *e, int argc,
                   char **argv, char *render) {
    (void)gui_window_draw_rect(wid, 1, 0, WIN_W - 2,
                               WIN_H - TITLE_BAR_H - 2, COLOR_BG);
    (void)gui_window_draw_text(wid, 12, 8, COLOR_STATUS,
                               "CTRL S SAVE CTRL Q CLOSE");

    if (argc > 1) {
        size_t i = 0;
        const char *prefix = "ARGV: ";
        while (prefix[i] != '\0' && i + 1 < RENDER_COLS) {
            render[i] = prefix[i];
            i++;
        }
        for (int a = 0; a < argc && i + 1 < RENDER_COLS; a++) {
            if (i + 1 < RENDER_COLS) {
                render[i++] = ' ';
            }
            const char *s = (a < argc && argv[a] != 0) ? argv[a] : "";
            for (size_t k = 0; s[k] != '\0' && i + 1 < RENDER_COLS; k++) {
                render[i++] = s[k];
            }
        }
        render[i] = '\0';
        (void)gui_window_draw_text(wid, 12, 22, COLOR_STATUS, render);
    }

    (void)editor_draw_line(wid, e, render, RENDER_COLS);

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

// Arrow keys come in as synthetic codes from drivers/input/input.c.
#define INPUT_KEY_LEFT  0x103
#define INPUT_KEY_RIGHT 0x104
#define INPUT_KEY_UP    0x101
#define INPUT_KEY_DOWN  0x102

int main(int argc, char **argv) {
    char path[PATH_CAP];
    editor_state_t e;
    char render[RENDER_COLS];
    gui_event_t events[EVENT_CAP];

    write_cstr(1, "editor: starting\n");
    print_argv(argc, argv);

    resolve_path(argc, argv, path);
    editor_init(&e);
    editor_load(&e, path);

    long wid = gui_window_create(WIN_X, WIN_Y, WIN_W, WIN_H,
                                 COLOR_BG, COLOR_BORDER, "editor");
    if (wid < 0) {
        write_cstr(1, "editor: window create failed\n");
        return 1;
    }
    (void)gui_window_set_title(wid, "editor", TITLE_BAR_H);

    redraw(wid, &e, argc, argv, render);

    for (;;) {
        long n = gui_window_event(wid, events, EVENT_CAP);
        if (n > 0) {
            int dirty = 0;
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
                    editor_save(&e, path);
                } else if (key == INPUT_KEY_LEFT) {
                    editor_left(&e);
                    dirty = 1;
                } else if (key == INPUT_KEY_RIGHT) {
                    editor_right(&e);
                    dirty = 1;
                } else if (key == INPUT_KEY_UP) {
                    editor_up(&e);
                    dirty = 1;
                } else if (key == INPUT_KEY_DOWN) {
                    editor_down(&e);
                    dirty = 1;
                } else if (key == 8 || key == 127) {
                    if (editor_backspace(&e) == 0) {
                        dirty = 1;
                    }
                } else if (key == 13 || key == 10) {
                    if (editor_insert(&e, '\n') == 0) {
                        dirty = 1;
                    }
                } else if (key >= 32 && key <= 126) {
                    if (editor_insert(&e, (char)key) == 0) {
                        dirty = 1;
                    }
                }
            }
            if (dirty) {
                redraw(wid, &e, argc, argv, render);
            }
        } else {
            (void)kli_yield();
        }
    }
}
