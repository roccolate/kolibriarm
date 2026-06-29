// ArmoniOS app: shell (C version, on libkarm + libkarmdesk)
//
// Windowed command shell with a circular log buffer, scrollback,
// bottom-anchored prompt, and command history. Reads key events
// from its GUI window.
//
// Display layout (top to bottom):
//   [history ..          ]
//   [.. scrolled view .. ]  <-- DISPLAY_LINES rows pulled from log[]
//   [.. top of log      ]
//   <cwd>               <-- current working directory
//   > <line>            <-- prompt row, always near the bottom
//   ENTER RUNS COMMAND  <-- footer
//
// PgUp / PgDn scroll the log view; scroll_offset resets to 0 when
// the user types a printable key or presses Enter (output follows
// input again). Up/Down walk the command history independently of
// the log scroll position.
//
// Non-window syscalls (spawn, spawn_argv, kill, readdir, timeinfo,
// meminfo, proclist, open/read/close, write, yield, exit) go through
// libkarm's typed wrappers. Number formatting and string compare go through
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
#define DISPLAY_LINES   10
#define DISPLAY_COLS    48
#define IO_BUF_CAP     128
#define EVENT_CAP        8
#define PROC_CAP         4
#define ARGV_MAX         8
#define HISTORY_DEPTH    4
#define CAT_MAX_CHUNKS   8
/* Circular log buffer depth. Each entry is a fixed LINE_CAP-byte
 * cstring. LOG_DEPTH controls how far the user can scroll back from
 * the latest output via Page Up. The shell's persistent state is
 * mapped with SYS_MMAP in main() instead of living in BSS or on the
 * 4 KB process stack, so the log depth can be chosen for usability
 * rather than stack pressure. */
#define LOG_DEPTH       16

#define COLOR_BG         0xff141820U
#define COLOR_BORDER     0xff708870U
#define COLOR_TEXT       0xffd8e8d8U

#define O_RDONLY         0

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
    /* Command history: fixed-depth ring of past input lines. */
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
    char   cwd[LINE_CAP];
    char   arg_buf[LINE_CAP];
    char   path_buf[LINE_CAP];
    char   io_buf[IO_BUF_CAP];
    char   emit_buf[DISPLAY_COLS];
    /* Syscall scratch buffers. */
    uint64_t   info[3];
    char       numbuf[24];
    char       display_lines[DISPLAY_LINES][DISPLAY_COLS];
    proc_entry_t procs[PROC_CAP];
    char       argv_strs[ARGV_MAX][LINE_CAP];
    uint64_t   argv_ptrs[ARGV_MAX];
    gui_event_t events[EVENT_CAP];
} shell_state_t;

typedef enum {
    SHELL_ARG_MISSING = 0,
    SHELL_ARG_OK = 1,
    SHELL_ARG_TOO_MANY = -1,
    SHELL_ARG_TOO_LONG = -2,
} shell_arg_result_t;

static void log_push(shell_state_t *s, const char *line) {
    (void)strlcpy(s->log[s->log_head], line, LINE_CAP);
    s->log_head = (s->log_head + 1) % LOG_DEPTH;
    if (s->log_count < LOG_DEPTH) {
        s->log_count++;
    }
}

// Read the log entry `lines_back` lines before the head (0 = newest).
// Always writes a NUL-terminated line into the caller-provided buffer.
static void log_read(const shell_state_t *s, int lines_back,
                     char *out, size_t out_size) {
    if (lines_back < 0) {
        lines_back = 0;
    }
    if (lines_back >= s->log_count) {
        out[0] = '\0';
        return;
    }
    int idx = (s->log_head - 1 - lines_back + LOG_DEPTH) % LOG_DEPTH;
    (void)strlcpy(out, s->log[idx], out_size);
}

static void history_push(shell_state_t *s) {
    if (s->line_len == 0) {
        return;
    }
    if (s->history_count < HISTORY_DEPTH) {
        (void)strlcpy(s->history[s->history_count], s->line, LINE_CAP);
        s->history_count++;
    } else {
        memmove(s->history[0], s->history[1],
                (HISTORY_DEPTH - 1) * LINE_CAP);
        (void)strlcpy(s->history[HISTORY_DEPTH - 1], s->line, LINE_CAP);
    }
    s->history_cursor = s->history_count;
}

static void history_load(shell_state_t *s, int idx) {
    size_t n = strlcpy(s->line, s->history[idx], LINE_CAP);
    if (n >= LINE_CAP) {
        n = LINE_CAP - 1;
    }
    s->line_len = n;
}

// Render the visible log window into `out_lines` (DISPLAY_LINES
// rows of DISPLAY_COLS bytes each). When scroll_offset is 0 the
// view shows the most recent DISPLAY_LINES lines; when >0 it walks
// backwards from the head.
static void render_log_view(shell_state_t *s, char (*out_lines)[DISPLAY_COLS]) {
    for (int i = 0; i < DISPLAY_LINES; i++) {
        out_lines[i][0] = '\0';
    }
    for (int row = 0; row < DISPLAY_LINES; row++) {
        /* The bottom-most visible row in the rendered list corresponds
         * to (scroll_offset) lines back from the head. Going up the
         * list goes further back. */
        int lines_back = s->scroll_offset + (DISPLAY_LINES - 1 - row);
        if (lines_back >= s->log_count) {
            continue;
        }
        log_read(s, lines_back, out_lines[row], DISPLAY_COLS);
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
    /* Prompt rows sit just below the log view, anchored to the
     * bottom of the visible area regardless of scroll_offset. */
    long prompt_y = 28 + DISPLAY_LINES * 16;
    draw_text(s->wid, 12, prompt_y, s->cwd);
    draw_text(s->wid, 12, prompt_y + 16, ">");
    draw_text(s->wid, 32, prompt_y + 16, s->line);
    draw_text(s->wid, 12, prompt_y + 36, "ENTER RUNS COMMAND");
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

static void shell_exit(shell_state_t *s) {
    (void)gui_window_destroy(s->wid);
    kli_exit(0);
    for (;;) {
        (void)kli_yield();
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

static shell_arg_result_t parse_one_arg(const char *input, char *out,
                                        size_t out_size) {
    while (*input == ' ') {
        input++;
    }
    if (*input == '\0') {
        out[0] = '\0';
        return SHELL_ARG_MISSING;
    }

    size_t len = 0;
    while (input[len] != '\0' && input[len] != ' ') {
        if (len + 1 >= out_size) {
            out[0] = '\0';
            return SHELL_ARG_TOO_LONG;
        }
        out[len] = input[len];
        len++;
    }
    out[len] = '\0';

    input += len;
    while (*input == ' ') {
        input++;
    }
    if (*input != '\0') {
        out[0] = '\0';
        return SHELL_ARG_TOO_MANY;
    }
    return SHELL_ARG_OK;
}

static int path_copy(char *out, size_t out_size, const char *src) {
    return strlcpy(out, src, out_size) < out_size ? 0 : -1;
}

static int path_is_root(const char *path) {
    return path[0] == '/' && path[1] == '\0';
}

static void path_trim_trailing_slashes(char *path) {
    size_t len = 0;
    while (path[len] != '\0') {
        len++;
    }
    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }
}

static void path_parent(char *path) {
    if (path_is_root(path)) {
        return;
    }
    size_t len = 0;
    while (path[len] != '\0') {
        len++;
    }
    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }
    while (len > 1 && path[len - 1] != '/') {
        len--;
    }
    if (len <= 1) {
        path[0] = '/';
        path[1] = '\0';
    } else {
        path[len - 1] = '\0';
    }
}

static int resolve_path(const shell_state_t *s, const char *arg,
                        char *out, size_t out_size) {
    if (arg[0] == '\0' || strcmp(arg, ".") == 0) {
        return path_copy(out, out_size, s->cwd);
    }
    if (strcmp(arg, "..") == 0) {
        if (path_copy(out, out_size, s->cwd) != 0) {
            return -1;
        }
        path_parent(out);
        return 0;
    }
    if (arg[0] == '/') {
        if (path_copy(out, out_size, arg) != 0) {
            return -1;
        }
        path_trim_trailing_slashes(out);
        return 0;
    }

    size_t pos = 0;
    if (path_is_root(s->cwd)) {
        if (out_size < 2) {
            return -1;
        }
        out[pos++] = '/';
    } else {
        size_t n = strlcpy(out, s->cwd, out_size);
        if (n >= out_size || n + 1 >= out_size) {
            return -1;
        }
        pos = n;
        out[pos++] = '/';
    }

    size_t i = 0;
    while (arg[i] != '\0') {
        if (pos + 1 >= out_size) {
            out[0] = '\0';
            return -1;
        }
        out[pos++] = arg[i++];
    }
    out[pos] = '\0';
    path_trim_trailing_slashes(out);
    return 0;
}

static void shell_emit_buffer(shell_state_t *s, const char *buf, long len) {
    size_t col = 0;

    for (long i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            if (col > 0) {
                s->emit_buf[col] = '\0';
                shell_emit(s, s->emit_buf);
                col = 0;
            }
            continue;
        }
        if (c < 32 || c > 126) {
            c = ' ';
        }
        if (col + 1 >= DISPLAY_COLS) {
            s->emit_buf[col] = '\0';
            shell_emit(s, s->emit_buf);
            col = 0;
        }
        s->emit_buf[col++] = c;
    }

    if (col > 0) {
        s->emit_buf[col] = '\0';
        shell_emit(s, s->emit_buf);
    }
}

static void cmd_run(shell_state_t *s, const char *input) {
    while (*input == ' ') {
        input++;
    }
    if (*input == '\0') {
        shell_emit(s, "RUN: MISSING APP");
        return;
    }
    int argc = 0;
    const char *p = input;
    while (*p != '\0') {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0' || *p == ' ') {
            break;
        }
        if (argc >= ARGV_MAX) {
            shell_emit(s, "RUN: TOO MANY ARGS");
            return;
        }
        const char *start = p;
        while (*p != '\0' && *p != ' ') {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len >= LINE_CAP) {
            shell_emit(s, "RUN: ARG TOO LONG");
            return;
        }
        for (size_t i = 0; i < len; i++) {
            s->argv_strs[argc][i] = start[i];
        }
        s->argv_strs[argc][len] = '\0';
        s->argv_ptrs[argc] = (uint64_t)(uintptr_t)s->argv_strs[argc];
        argc++;
    }
    if (argc == 0) {
        shell_emit(s, "RUN: MISSING APP");
        return;
    }
    const char *prefix = "/armonios/";
    size_t pi = strlcpy(s->path_buf, prefix, sizeof(s->path_buf));
    if (pi >= sizeof(s->path_buf)) {
        shell_emit(s, "RUN: PATH TOO LONG");
        return;
    }
    for (size_t i = 0; s->argv_strs[0][i] != '\0'; i++) {
        if (pi + 1 >= sizeof(s->path_buf)) {
            shell_emit(s, "RUN: APP NAME TOO LONG");
            return;
        }
        s->path_buf[pi++] = s->argv_strs[0][i];
    }
    s->path_buf[pi] = '\0';

    long pid = kli_spawn_argv(s->path_buf, 0,
                              (const long *)s->argv_ptrs, (long)argc);
    if (pid < 0) {
        shell_emit(s, "RUN: NOT FOUND");
        return;
    }
    s->last_spawned_pid = (int)pid;
    shell_emit(s, "RUN: SPAWNED PID");
    kli_utoa((uint64_t)pid, s->numbuf, sizeof(s->numbuf));
    shell_emit(s, s->numbuf);
}

static void cmd_pwd(shell_state_t *s) {
    shell_emit(s, s->cwd);
}

static void cmd_cd(shell_state_t *s, const char *input) {
    shell_arg_result_t arg = parse_one_arg(input, s->arg_buf,
                                           sizeof(s->arg_buf));
    if (arg == SHELL_ARG_TOO_MANY) {
        shell_emit(s, "CD: TOO MANY ARGS");
        return;
    }
    if (arg == SHELL_ARG_TOO_LONG) {
        shell_emit(s, "CD: PATH TOO LONG");
        return;
    }

    if (arg == SHELL_ARG_MISSING) {
        (void)path_copy(s->cwd, sizeof(s->cwd), "/");
        shell_emit(s, "CD: /");
        return;
    }
    if (resolve_path(s, s->arg_buf, s->path_buf, sizeof(s->path_buf)) != 0) {
        shell_emit(s, "CD: PATH TOO LONG");
        return;
    }
    if (kli_readdir(s->path_buf, s->io_buf, sizeof(s->io_buf)) < 0) {
        shell_emit(s, "CD: NOT A DIR");
        return;
    }

    (void)path_copy(s->cwd, sizeof(s->cwd), s->path_buf);
    shell_emit(s, "CD:");
    shell_emit(s, s->cwd);
}

static void cmd_ls(shell_state_t *s, const char *input) {
    shell_arg_result_t arg = parse_one_arg(input, s->arg_buf,
                                           sizeof(s->arg_buf));

    if (arg == SHELL_ARG_TOO_MANY) {
        shell_emit(s, "LS: TOO MANY ARGS");
        return;
    }
    if (arg == SHELL_ARG_TOO_LONG) {
        shell_emit(s, "LS: PATH TOO LONG");
        return;
    }
    if (arg == SHELL_ARG_MISSING) {
        s->arg_buf[0] = '\0';
    }
    if (resolve_path(s, s->arg_buf, s->path_buf, sizeof(s->path_buf)) != 0) {
        shell_emit(s, "LS: PATH TOO LONG");
        return;
    }

    long n = kli_readdir(s->path_buf, s->io_buf, sizeof(s->io_buf));
    if (n < 0) {
        shell_emit(s, "LS: FAILED");
    } else if (n == 0) {
        shell_emit(s, "LS: EMPTY");
    } else {
        shell_emit_buffer(s, s->io_buf, n);
    }
}

static void cmd_cat(shell_state_t *s, const char *input) {
    shell_arg_result_t arg = parse_one_arg(input, s->arg_buf,
                                           sizeof(s->arg_buf));
    if (arg == SHELL_ARG_MISSING) {
        shell_emit(s, "CAT: MISSING PATH");
        return;
    }
    if (arg == SHELL_ARG_TOO_MANY) {
        shell_emit(s, "CAT: TOO MANY ARGS");
        return;
    }
    if (arg == SHELL_ARG_TOO_LONG ||
        resolve_path(s, s->arg_buf, s->path_buf, sizeof(s->path_buf)) != 0) {
        shell_emit(s, "CAT: PATH TOO LONG");
        return;
    }

    long fd = kli_open(s->path_buf, O_RDONLY);
    if (fd < 0) {
        shell_emit(s, "CAT: OPEN FAILED");
        return;
    }

    int chunks = 0;
    for (;;) {
        long n = kli_read((int)fd, s->io_buf, sizeof(s->io_buf));
        if (n < 0) {
            shell_emit(s, "CAT: READ FAILED");
            break;
        }
        if (n == 0) {
            if (chunks == 0) {
                shell_emit(s, "CAT: EMPTY");
            }
            break;
        }
        shell_emit_buffer(s, s->io_buf, n);
        chunks++;
        if (chunks >= CAT_MAX_CHUNKS) {
            shell_emit(s, "CAT: TRUNCATED");
            break;
        }
    }
    (void)kli_close((int)fd);
}

static void dispatch(shell_state_t *s, const char *line) {
    if (strcmp(line, "help") == 0) {
        shell_emit(s, "HELP PWD CD LS CAT RUN KILL EXIT");
        shell_emit(s, "HELP MEM PS TICKS");
    } else if (strcmp(line, "pwd") == 0) {
        cmd_pwd(s);
    } else if (strcmp(line, "cd") == 0) {
        cmd_cd(s, "");
    } else if (starts_with(line, "cd ")) {
        cmd_cd(s, line + 3);
    } else if (strcmp(line, "ls") == 0) {
        cmd_ls(s, "");
    } else if (starts_with(line, "ls ")) {
        cmd_ls(s, line + 3);
    } else if (starts_with(line, "cat ")) {
        cmd_cat(s, line + 4);
    } else if (strcmp(line, "cat") == 0) {
        cmd_cat(s, "");
    } else if (strcmp(line, "mem") == 0) {
        if (kli_meminfo(s->info) < 0) {
            shell_emit(s, "MEM: FAILED");
        } else {
            shell_emit(s, "MEM: FREE PAGES");
            kli_utoa(s->info[1], s->numbuf, sizeof(s->numbuf));
            shell_emit(s, s->numbuf);
        }
    } else if (strcmp(line, "ps") == 0) {
        long n = kli_proclist(s->procs, PROC_CAP);
        if (n < 0) {
            shell_emit(s, "PS: FAILED");
            return;
        }
        shell_emit(s, "PS: PROCESS NAMES");
        for (long i = 0; i < n && i < PROC_CAP; i++) {
            shell_emit(s, s->procs[i].name);
        }
    } else if (strcmp(line, "ticks") == 0) {
        if (kli_timeinfo(s->info) < 0) {
            shell_emit(s, "TICKS: FAILED");
        } else {
            shell_emit(s, "TICKS: TIMER");
            kli_utoa(s->info[0], s->numbuf, sizeof(s->numbuf));
            shell_emit(s, s->numbuf);
        }
    } else if (starts_with(line, "run ")) {
        cmd_run(s, line + 4);
    } else if (strcmp(line, "kill last") == 0) {
        if (s->last_spawned_pid == 0) {
            shell_emit(s, "KILL: NO LAST PID");
        } else if (kli_kill((long)s->last_spawned_pid) < 0) {
            s->last_spawned_pid = 0;
            shell_emit(s, "KILL: FAILED");
        } else {
            s->last_spawned_pid = 0;
            shell_emit(s, "KILL: SENT");
        }
    } else if (strcmp(line, "exit") == 0) {
        shell_exit(s);
    } else {
        shell_emit(s, "UNKNOWN COMMAND");
    }
}

static void handle_key(shell_state_t *s, int key) {
    if (key == 17) {
        shell_exit(s);
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
            s->line[0] = '\0';
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
        (void)strlcpy(submitted, s->line, sizeof(submitted));
        history_push(s);
        shell_emit(s, submitted);
        dispatch(s, submitted);
        s->line[0] = '\0';
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
     * The loader only copies the KLI1 image bytes (header + code +
     * rodata), so .bss is not backed by user pages. Keep the large,
     * mutable shell_state_t in anonymous user memory instead of
     * spending most of the fixed 4 KB process stack.
     */
    long state_addr = kli_mmap(0, sizeof(shell_state_t), 0);
    if (state_addr < 0) {
        kli_write_cstr(1, "shell: state mmap failed\n");
        return 1;
    }
    shell_state_t *s = (shell_state_t *)(uintptr_t)state_addr;
    memset(s, 0, sizeof(*s));
    (void)path_copy(s->cwd, sizeof(s->cwd), "/");

    kli_write_cstr(1, "shell: starting\n");

    s->wid = gui_window_create(WIN_X, WIN_Y, WIN_W, WIN_H,
                               COLOR_BG, COLOR_BORDER, "shell");
    if (s->wid < 0) {
        kli_write_cstr(1, "shell: window create failed\n");
        return 1;
    }
    (void)gui_window_set_title(s->wid, "shell", TITLE_BAR_H);

    shell_emit(s, "SHELL READY");
    redraw(s);

    for (;;) {
        long n = gui_window_event(s->wid, s->events, EVENT_CAP);
        if (n > 0) {
            int dirty = 0;
            for (long i = 0; i < n; i++) {
                if (s->events[i].type == GUI_EVENT_CLOSE) {
                    (void)gui_window_destroy(s->wid);
                    return 0;
                }
                if (s->events[i].type == GUI_EVENT_KEY_PRESS) {
                    handle_key(s, s->events[i].data1);
                    dirty = 1;
                }
            }
            if (dirty) {
                redraw(s);
            }
        } else {
            (void)kli_yield();
        }
    }
}
