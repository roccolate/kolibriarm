// KolibriARM app: shell (C version, on libkarm + libkarmdesk)
//
// Windowed command shell with a circular log buffer, scrollback,
// bottom-anchored prompt, and command history. Reads key events
// from its GUI window.
//
// Display layout (top to bottom):
//   [history ..          ]
//   [.. scrolled view .. ]  <-- DISPLAY_LINES rows pulled from log[]
//   [.. top of log      ]
//   U <line>            <-- prompt row, always at the bottom
//   ENTER RUNS COMMAND  <-- footer
//
// PgUp / PgDn scroll the log view; scroll_offset resets to 0 when
// the user types a printable key or presses Enter (output follows
// input again). Up/Down walk the command history independently of
// the log scroll position.
//
// Non-window syscalls (spawn, spawn_argv, kill, readdir, timeinfo,
// meminfo, proclist, write, yield, exit) go through libkarm's typed
// wrappers. Number formatting and string compare go through
// <libkarm/string.h>. Window syscalls go through libkarmdesk's
// typed gui_* wrappers.

#include <stddef.h>
#include <stdint.h>

#include "libkarm/syscall.h"
#include "libkarm/string.h"
#include "libkarmdesk/gui.h"

#define WIN_X           72
#define WIN_Y           52
#define WIN_W          480
#define WIN_H          280
#define TITLE_BAR_H     16
#define LINE_CAP        64
#define DISPLAY_LINES    7
#define DISPLAY_COLS    48
#define EVENT_CAP        8
#define PROC_CAP         4
#define ARGV_MAX         8
#define HISTORY_DEPTH    4
/* Circular log buffer depth. Each entry is a fixed LINE_CAP-byte
 * cstring. LOG_DEPTH controls how far the user can scroll back from
 * the latest output via Page Up. The shell_state_t struct lives on
 * the C stack (the loader only copies KLI1 image bytes, BSS is
 * unmapped), so LOG_DEPTH * LINE_CAP must stay under a kilobyte or
 * two of the 4 KB kernel stack. 16 entries = 1 KB of log, plus
 * history (256 B), procs and argv (~1 KB), still leaves room for
 * the rest of the struct and the live call stack. */
#define LOG_DEPTH       16

#define COLOR_BG         0xff141820U
#define COLOR_BORDER     0xff708870U
#define COLOR_TEXT       0xffd8e8d8U

// Arrow keys come in as synthetic codes from drivers/input/input.c.
#define INPUT_KEY_UP    0x101U
#define INPUT_KEY_DOWN  0x102U
#define INPUT_KEY_PGUP  0x105U
#define INPUT_KEY_PGDN  0x106U

// sys_proclist entry: pid(u32) state(u32) name[16] -> 24 bytes.
typedef struct {
    uint32_t pid;
    uint32_t state;
    char     name[16];
} proc_entry_t;

typedef struct {
    /* Current input line being edited. */
    char   line[LINE_CAP];
    size_t line_len;
    /* Command history: depth-8 ring of past input lines. */
    char   history[HISTORY_DEPTH][LINE_CAP];
    int    history_count;
    int    history_cursor;   // == history_count means "live"
    int    last_spawned_pid;
    /* Circular output log. log[(log_head - 1 - i) mod LOG_DEPTH] for
     * i in 0..log_count-1 gives the last i+1 entries in newest-first
     * order; log_count is capped at LOG_DEPTH. */
    char   log[LOG_DEPTH][LINE_CAP];
    int    log_head;        // index of next slot to write
    int    log_count;       // total entries currently stored (<= LOG_DEPTH)
    /* Scroll offset in lines from the bottom of the log. 0 means
     * "follow output" — new push_line entries scroll the view back
     * to the bottom. >0 freezes the view on older entries. */
    int    scroll_offset;
    long   wid;
    /* Syscall scratch buffers. */
    uint64_t   info[3];
    char       numbuf[24];
    char       display_lines[DISPLAY_LINES][DISPLAY_COLS];
    proc_entry_t procs[PROC_CAP];
    char       argv_strs[ARGV_MAX][LINE_CAP];
    uint64_t   argv_ptrs[ARGV_MAX];
} shell_state_t;

static void log_push(shell_state_t *s, const char *line) {
    size_t n = 0;
    while (n + 1 < LINE_CAP && line[n] != '\0') {
        s->log[s->log_head][n] = line[n];
        n++;
    }
    s->log[s->log_head][n] = '\0';
    s->log_head = (s->log_head + 1) % LOG_DEPTH;
    if (s->log_count < LOG_DEPTH) {
        s->log_count++;
    }
}

// Read the log entry `lines_back` lines before the head (0 = newest).
// Always writes into a NUL-terminated LINE_CAP buffer.
static void log_read(const shell_state_t *s, int lines_back, char *out) {
    if (lines_back < 0) {
        lines_back = 0;
    }
    if (lines_back >= s->log_count) {
        out[0] = '\0';
        return;
    }
    int idx = (s->log_head - 1 - lines_back + LOG_DEPTH) % LOG_DEPTH;
    size_t i = 0;
    while (i + 1 < LINE_CAP && s->log[idx][i] != '\0') {
        out[i] = s->log[idx][i];
        i++;
    }
    out[i] = '\0';
}

static void history_push(shell_state_t *s) {
    if (s->line_len == 0) {
        return;
    }
    if (s->history_count < HISTORY_DEPTH) {
        for (size_t i = 0; i < s->line_len; i++) {
            s->history[s->history_count][i] = s->line[i];
        }
        s->history[s->history_count][s->line_len] = '\0';
        s->history_count++;
    } else {
        for (int i = 0; i < HISTORY_DEPTH - 1; i++) {
            for (size_t j = 0; j < LINE_CAP; j++) {
                s->history[i][j] = s->history[i + 1][j];
            }
        }
        for (size_t i = 0; i < s->line_len; i++) {
            s->history[HISTORY_DEPTH - 1][i] = s->line[i];
        }
        s->history[HISTORY_DEPTH - 1][s->line_len] = '\0';
    }
    s->history_cursor = s->history_count;
}

static void history_load(shell_state_t *s, int idx) {
    for (size_t i = 0; i < LINE_CAP; i++) {
        s->line[i] = '\0';
    }
    size_t n = 0;
    while (s->history[idx][n] != '\0' && n + 1 < LINE_CAP) {
        s->line[n] = s->history[idx][n];
        n++;
    }
    s->line[n] = '\0';
    s->line_len = n;
}

// Render the visible log window into `out_lines` (DISPLAY_LINES
// rows of DISPLAY_COLS bytes each). When scroll_offset is 0 the
// view shows the most recent DISPLAY_LINES lines; when >0 it walks
// backwards from the head.
static void render_log_view(shell_state_t *s, char (*out_lines)[DISPLAY_COLS]) {
    for (int i = 0; i < DISPLAY_LINES; i++) {
        for (int j = 0; j < DISPLAY_COLS; j++) {
            out_lines[i][j] = '\0';
        }
    }
    for (int row = 0; row < DISPLAY_LINES; row++) {
        /* The bottom-most visible row in the rendered list corresponds
         * to (scroll_offset) lines back from the head. Going up the
         * list goes further back. */
        int lines_back = s->scroll_offset + (DISPLAY_LINES - 1 - row);
        if (lines_back >= s->log_count) {
            continue;
        }
        char tmp[LINE_CAP];
        log_read(s, lines_back, tmp);
        for (int j = 0; j < DISPLAY_COLS - 1 && tmp[j] != '\0'; j++) {
            out_lines[row][j] = tmp[j];
        }
    }
}

static void draw_text(long wid, long x, long y, const char *s) {
    (void)gui_window_draw_text(wid, x, y, COLOR_TEXT, s);
}

static void redraw(shell_state_t *s) {
    (void)gui_window_draw_rect(s->wid, 1, 0, WIN_W - 2,
                               WIN_H - TITLE_BAR_H - 2, COLOR_BG);
    draw_text(s->wid, 12, 8, "USER SHELL");
    render_log_view(s, s->display_lines);
    for (int i = 0; i < DISPLAY_LINES; i++) {
        draw_text(s->wid, 12, 28 + i * 16, s->display_lines[i]);
    }
    /* Prompt row sits just below the log view, anchored to the
     * bottom of the visible area regardless of scroll_offset. */
    long prompt_y = 28 + DISPLAY_LINES * 16;
    draw_text(s->wid, 12, prompt_y, "U");
    draw_text(s->wid, 32, prompt_y, s->line);
    draw_text(s->wid, 12, prompt_y + 28, "ENTER RUNS COMMAND");
    (void)gui_window_flush(s->wid, 0, 0, WIN_W, WIN_H - TITLE_BAR_H);
}

// Append `text` to the log and re-render.
static void shell_emit(shell_state_t *s, const char *text) {
    log_push(s, text);
    // Auto-follow: if the user is at the bottom, snap the view.
    if (s->scroll_offset > 0) {
        s->scroll_offset = 0;
    }
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix != '\0') {
        if (*s != *prefix) {
            return 0;
        }
        s++;
        prefix++;
    }
    return 1;
}

static void cmd_run(shell_state_t *s, const char *input) {
    while (*input == ' ') {
        input++;
    }
    if (*input == '\0') {
        shell_emit(s, "RUN FAILED");
        return;
    }
    int argc = 0;
    const char *p = input;
    while (*p != '\0' && argc < ARGV_MAX) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0' || *p == ' ') {
            break;
        }
        const char *start = p;
        while (*p != '\0' && *p != ' ') {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len >= LINE_CAP) {
            len = LINE_CAP - 1;
        }
        for (size_t i = 0; i < len; i++) {
            s->argv_strs[argc][i] = start[i];
        }
        s->argv_strs[argc][len] = '\0';
        s->argv_ptrs[argc] = (uint64_t)(uintptr_t)s->argv_strs[argc];
        argc++;
    }
    if (argc == 0) {
        shell_emit(s, "RUN FAILED");
        return;
    }
    char path[32];
    const char *prefix = "/kolibri/";
    size_t pi = 0;
    while (prefix[pi] != '\0') {
        path[pi] = prefix[pi];
        pi++;
    }
    for (size_t i = 0; s->argv_strs[0][i] != '\0' && pi + 1 < sizeof(path); i++) {
        path[pi++] = s->argv_strs[0][i];
    }
    path[pi] = '\0';

    long pid = kli_spawn_argv(path, 0, (const long *)s->argv_ptrs, (long)argc);
    if (pid < 0) {
        shell_emit(s, "RUN FAILED");
        return;
    }
    s->last_spawned_pid = (int)pid;
    shell_emit(s, "SPAWNED");
}

static void dispatch(shell_state_t *s, const char *line) {
    if (strcmp(line, "help") == 0) {
        shell_emit(s, "HELP LS MEM PS TICKS RUN NAME KILL LAST EXIT");
    } else if (strcmp(line, "ls") == 0) {
        char buf[DISPLAY_COLS];
        long n = kli_readdir("/", buf, (long)sizeof(buf));
        if (n < 0) {
            shell_emit(s, "LS FAILED");
        } else {
            if (n >= DISPLAY_COLS) n = DISPLAY_COLS - 1;
            buf[n] = '\0';
            shell_emit(s, buf);
        }
    } else if (strcmp(line, "mem") == 0) {
        if (kli_meminfo(s->info) < 0) {
            shell_emit(s, "MEM FAILED");
        } else {
            shell_emit(s, "FREE PAGES");
            kli_utoa(s->info[1], s->numbuf, sizeof(s->numbuf));
            shell_emit(s, s->numbuf);
        }
    } else if (strcmp(line, "ps") == 0) {
        long n = kli_proclist(s->procs, PROC_CAP);
        if (n < 0) {
            shell_emit(s, "PS FAILED");
            return;
        }
        shell_emit(s, "PROCESSES");
        for (long i = 0; i < n && i < PROC_CAP; i++) {
            char name_buf[17];
            for (int j = 0; j < 16; j++) {
                char c = s->procs[i].name[j];
                name_buf[j] = (c == '\0') ? '\0' : c;
            }
            name_buf[16] = '\0';
            shell_emit(s, name_buf);
        }
    } else if (strcmp(line, "ticks") == 0) {
        if (kli_timeinfo(s->info) < 0) {
            shell_emit(s, "TICKS FAILED");
        } else {
            shell_emit(s, "TIMER TICKS");
            kli_utoa(s->info[0], s->numbuf, sizeof(s->numbuf));
            shell_emit(s, s->numbuf);
        }
    } else if (starts_with(line, "run ")) {
        cmd_run(s, line + 4);
    } else if (strcmp(line, "kill last") == 0) {
        if (s->last_spawned_pid == 0) {
            shell_emit(s, "KILL FAILED");
        } else {
            (void)kli_kill((long)s->last_spawned_pid);
            s->last_spawned_pid = 0;
            shell_emit(s, "KILLED");
        }
    } else if (strcmp(line, "pwd") == 0) {
        shell_emit(s, "ROOT");
    } else if (strcmp(line, "exit") == 0) {
        (void)gui_window_destroy(s->wid);
        kli_exit(0);
        for (;;) {
            (void)kli_yield();
        }
    } else {
        shell_emit(s, "UNKNOWN COMMAND");
    }
}

static void handle_key(shell_state_t *s, int key) {
    if (key == 17) {
        (void)gui_window_destroy(s->wid);
        kli_exit(0);
        for (;;) {
            (void)kli_yield();
        }
    }
    if (key == INPUT_KEY_UP) {
        if (s->history_cursor > 0) {
            s->history_cursor--;
            history_load(s, s->history_cursor);
        }
        return;
    }
    if (key == INPUT_KEY_DOWN) {
        if (s->history_cursor + 1 < s->history_count) {
            s->history_cursor++;
            history_load(s, s->history_cursor);
        } else if (s->history_cursor + 1 == s->history_count) {
            s->history_cursor = s->history_count;
            for (size_t i = 0; i < LINE_CAP; i++) {
                s->line[i] = '\0';
            }
            s->line_len = 0;
        }
        return;
    }
    if (key == INPUT_KEY_PGUP) {
        int max_off = s->log_count - 1;
        if (max_off < 0) {
            max_off = 0;
        }
        int next = s->scroll_offset + DISPLAY_LINES;
        if (next > max_off) {
            next = max_off;
        }
        s->scroll_offset = next;
        return;
    }
    if (key == INPUT_KEY_PGDN) {
        int next = s->scroll_offset - DISPLAY_LINES;
        if (next < 0) {
            next = 0;
        }
        s->scroll_offset = next;
        return;
    }
    if (key == 8 || key == 127) {
        if (s->line_len > 0) {
            s->line_len--;
            s->line[s->line_len] = '\0';
        }
        s->scroll_offset = 0;
        return;
    }
    if (key == 13 || key == 10) {
        s->line[s->line_len] = '\0';
        char submitted[LINE_CAP];
        for (size_t i = 0; i <= s->line_len; i++) {
            submitted[i] = s->line[i];
        }
        history_push(s);
        shell_emit(s, submitted);
        dispatch(s, submitted);
        for (size_t i = 0; i < s->line_len; i++) {
            s->line[i] = '\0';
        }
        s->line_len = 0;
        s->scroll_offset = 0;
        return;
    }
    if (key >= 32 && key <= 126) {
        if (s->line_len + 1 < LINE_CAP) {
            s->line[s->line_len++] = (char)key;
            s->line[s->line_len] = '\0';
        }
        s->scroll_offset = 0;
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /*
     * shell_state_t is ~2.4 KB and lives on the C stack instead of
     * BSS. The loader only copies the KLI1 image bytes (header +
     * code + rodata) into the per-process image slot, so a static
     * shell_state_t would land in unmapped memory and the first
     * write would fault with a translation abort. Same constraint
     * that drives panel.c to put panel_state_t on the stack.
     */
    shell_state_t s;
    s.line_len = 0;
    s.history_count = 0;
    s.history_cursor = 0;
    s.last_spawned_pid = 0;
    s.log_head = 0;
    s.log_count = 0;
    s.scroll_offset = 0;
    for (size_t i = 0; i < LINE_CAP; i++) {
        s.line[i] = '\0';
        for (int h = 0; h < HISTORY_DEPTH; h++) {
            s.history[h][i] = '\0';
        }
    }
    for (int i = 0; i < LOG_DEPTH; i++) {
        for (size_t j = 0; j < LINE_CAP; j++) {
            s.log[i][j] = '\0';
        }
    }

    kli_write_cstr(1, "shell: starting\n");

    s.wid = gui_window_create(WIN_X, WIN_Y, WIN_W, WIN_H,
                              COLOR_BG, COLOR_BORDER, "shell");
    if (s.wid < 0) {
        kli_write_cstr(1, "shell: window create failed\n");
        return 1;
    }
    (void)gui_window_set_title(s.wid, "shell", TITLE_BAR_H);

    shell_emit(&s, "SHELL READY");
    redraw(&s);

    gui_event_t events[EVENT_CAP];
    for (;;) {
        long n = gui_window_event(s.wid, events, EVENT_CAP);
        if (n > 0) {
            int dirty = 0;
            for (long i = 0; i < n; i++) {
                if (events[i].type == GUI_EVENT_CLOSE) {
                    (void)gui_window_destroy(s.wid);
                    return 0;
                }
                if (events[i].type == GUI_EVENT_KEY_PRESS) {
                    handle_key(&s, events[i].data1);
                    dirty = 1;
                }
            }
            if (dirty) {
                redraw(&s);
            }
        } else {
            (void)kli_yield();
        }
    }
}
