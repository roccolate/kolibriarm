// ArmoniOS app: files
//
// Small FAT-root file manager. It lists /fat, opens selected files in the
// editor, and supports create/rename/delete for FAT32 8.3 names.

#include <stddef.h>
#include <stdint.h>

#include "libkarm/syscall.h"
#include "libkarmdesk/gui.h"

#define O_RDWR          2
#define O_CREAT         0x40

#define WIN_X          112
#define WIN_Y           84
#define WIN_W          360
#define WIN_H          260
#define TITLE_BAR_H     16
#define EVENT_CAP        8

#define ENTRY_CAP        8
#define NAME_CAP        13
#define PATH_CAP        32
#define LIST_BUF_CAP   256
#define STATUS_CAP      40

#define COLOR_BG        0xff182028U
#define COLOR_BORDER    0xff809090U
#define COLOR_TEXT      0xffd8e8e8U
#define COLOR_DIM       0xff90a0a0U
#define COLOR_SELECT    0xff405870U
#define COLOR_WARN      0xffe0c080U

#define INPUT_KEY_UP    0x101
#define INPUT_KEY_DOWN  0x102

typedef enum {
    FILES_MODE_NORMAL = 0,
    FILES_MODE_NEW,
    FILES_MODE_RENAME,
    FILES_MODE_DELETE,
} files_mode_t;

typedef struct {
    long wid;
    int selected;
    int count;
    files_mode_t mode;
    char entries[ENTRY_CAP][NAME_CAP];
    char list_buf[LIST_BUF_CAP];
    char input[NAME_CAP];
    int input_len;
    char status[STATUS_CAP];
    char editor_argv0[8];
    char editor_path[PATH_CAP];
    uint64_t editor_argv[2];
    gui_event_t events[EVENT_CAP];
} files_state_t;

static void copy_cstr(char *dst, size_t dst_size, const char *src) {
    size_t i = 0;
    while (i + 1 < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int is_bad_83_char(char c) {
    return c <= ' ' || c == '"' || c == '*' || c == '+' || c == ',' ||
           c == ':' || c == ';' || c == '<' || c == '=' || c == '>' ||
           c == '?' || c == '[' || c == '\\' || c == ']' || c == '|' ||
           c == '/';
}

static char uppercase_83_char(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

static int valid_83_name(const char *name) {
    int base = 0;
    int ext = 0;
    int in_ext = 0;

    if (name[0] == '\0') {
        return 0;
    }

    for (int i = 0; name[i] != '\0'; i++) {
        char c = name[i];
        if (c == '.') {
            if (in_ext != 0 || base == 0) {
                return 0;
            }
            in_ext = 1;
            continue;
        }
        if (is_bad_83_char(c)) {
            return 0;
        }
        if (in_ext == 0) {
            base++;
            if (base > 8) {
                return 0;
            }
        } else {
            ext++;
            if (ext > 3) {
                return 0;
            }
        }
    }

    return base > 0 && (in_ext == 0 || ext > 0);
}

static void build_fat_path(char *out, size_t out_size, const char *name) {
    static const char prefix[] = "/fat/";
    size_t i = 0;
    size_t p = 0;

    while (p + 1 < out_size && prefix[p] != '\0') {
        out[p] = prefix[p];
        p++;
    }
    while (p + 1 < out_size && name[i] != '\0') {
        out[p++] = name[i++];
    }
    out[p] = '\0';
}

static void clear_entries(files_state_t *s) {
    for (int i = 0; i < ENTRY_CAP; i++) {
        s->entries[i][0] = '\0';
    }
    s->count = 0;
    s->selected = 0;
}

static void parse_list(files_state_t *s, long bytes) {
    int entry = 0;
    int col = 0;

    clear_entries(s);
    for (long i = 0; i < bytes && entry < ENTRY_CAP; i++) {
        char c = s->list_buf[i];
        if (c == '\n') {
            s->entries[entry][col] = '\0';
            if (col > 0) {
                entry++;
            }
            col = 0;
            continue;
        }
        if (col + 1 < NAME_CAP) {
            s->entries[entry][col++] = c;
        }
    }
    if (col > 0 && entry < ENTRY_CAP) {
        s->entries[entry][col] = '\0';
        entry++;
    }
    s->count = entry;
    if (s->selected >= s->count) {
        s->selected = s->count > 0 ? s->count - 1 : 0;
    }
}

static void refresh_list(files_state_t *s) {
    long n = kli_readdir("/fat", s->list_buf, sizeof(s->list_buf));
    if (n < 0) {
        clear_entries(s);
        copy_cstr(s->status, sizeof(s->status), "FAT UNAVAILABLE");
        return;
    }
    parse_list(s, n);
    copy_cstr(s->status, sizeof(s->status),
              s->count == 0 ? "NO FILES" : "FAT READY");
}

static void draw_text(long wid, long x, long y, uint32_t color,
                      const char *text) {
    (void)gui_window_draw_text(wid, x, y, color, text);
}

static void redraw(files_state_t *s) {
    (void)gui_window_draw_rect(s->wid, 1, 0, WIN_W - 2,
                               WIN_H - TITLE_BAR_H - 2, COLOR_BG);
    draw_text(s->wid, 12, 8, COLOR_TEXT, "FILES /fat");
    draw_text(s->wid, 12, 24, COLOR_DIM, "ENTER OPEN  N NEW  R RENAME  D DEL");
    draw_text(s->wid, 12, 40,
              s->mode == FILES_MODE_DELETE ? COLOR_WARN : COLOR_DIM,
              s->status);

    for (int i = 0; i < ENTRY_CAP; i++) {
        long y = 60 + i * 16;
        if (i == s->selected && s->count > 0) {
            (void)gui_window_draw_rect(s->wid, 8, y - 2, WIN_W - 16, 13,
                                       COLOR_SELECT);
        }
        if (i < s->count) {
            draw_text(s->wid, 16, y, COLOR_TEXT, s->entries[i]);
        }
    }

    if (s->mode == FILES_MODE_NEW) {
        draw_text(s->wid, 12, 204, COLOR_WARN, "NEW:");
        draw_text(s->wid, 64, 204, COLOR_TEXT, s->input);
    } else if (s->mode == FILES_MODE_RENAME) {
        draw_text(s->wid, 12, 204, COLOR_WARN, "RENAME:");
        draw_text(s->wid, 88, 204, COLOR_TEXT, s->input);
    } else if (s->mode == FILES_MODE_DELETE) {
        draw_text(s->wid, 12, 204, COLOR_WARN, "DELETE? Y/N");
    }

    (void)gui_window_flush(s->wid, 0, 0, WIN_W, WIN_H - TITLE_BAR_H);
}

static const char *selected_name(files_state_t *s) {
    if (s->count <= 0 || s->selected < 0 || s->selected >= s->count) {
        return 0;
    }
    return s->entries[s->selected];
}

static void open_selected(files_state_t *s) {
    const char *name = selected_name(s);
    if (name == 0) {
        copy_cstr(s->status, sizeof(s->status), "NO SELECTION");
        return;
    }

    build_fat_path(s->editor_path, sizeof(s->editor_path), name);
    copy_cstr(s->editor_argv0, sizeof(s->editor_argv0), "editor");
    s->editor_argv[0] = (uint64_t)(uintptr_t)s->editor_argv0;
    s->editor_argv[1] = (uint64_t)(uintptr_t)s->editor_path;

    if (kli_spawn_argv("/armonios/editor", 0,
                       (const long *)s->editor_argv, 2) < 0) {
        copy_cstr(s->status, sizeof(s->status), "OPEN FAILED");
    } else {
        copy_cstr(s->status, sizeof(s->status), "OPENED IN EDITOR");
    }
}

static void start_text_mode(files_state_t *s, files_mode_t mode) {
    s->mode = mode;
    s->input[0] = '\0';
    s->input_len = 0;
    copy_cstr(s->status, sizeof(s->status),
              mode == FILES_MODE_NEW ? "TYPE NEW 8.3 NAME" :
                                        "TYPE NEW NAME");
}

static void create_file(files_state_t *s) {
    char path[PATH_CAP];
    long fd;

    if (!valid_83_name(s->input)) {
        copy_cstr(s->status, sizeof(s->status), "BAD 8.3 NAME");
        return;
    }

    build_fat_path(path, sizeof(path), s->input);
    fd = kli_open(path, O_RDWR | O_CREAT);
    if (fd < 0) {
        copy_cstr(s->status, sizeof(s->status), "CREATE FAILED");
        return;
    }
    (void)kli_close((int)fd);
    refresh_list(s);
    copy_cstr(s->status, sizeof(s->status), "CREATED");
}

static void rename_file(files_state_t *s) {
    const char *name = selected_name(s);
    char old_path[PATH_CAP];
    char new_path[PATH_CAP];

    if (name == 0) {
        copy_cstr(s->status, sizeof(s->status), "NO SELECTION");
        return;
    }
    if (!valid_83_name(s->input)) {
        copy_cstr(s->status, sizeof(s->status), "BAD 8.3 NAME");
        return;
    }

    build_fat_path(old_path, sizeof(old_path), name);
    build_fat_path(new_path, sizeof(new_path), s->input);
    if (kli_rename(old_path, new_path) < 0) {
        copy_cstr(s->status, sizeof(s->status), "RENAME FAILED");
        return;
    }
    refresh_list(s);
    copy_cstr(s->status, sizeof(s->status), "RENAMED");
}

static void delete_file(files_state_t *s) {
    const char *name = selected_name(s);
    char path[PATH_CAP];

    if (name == 0) {
        copy_cstr(s->status, sizeof(s->status), "NO SELECTION");
        return;
    }
    build_fat_path(path, sizeof(path), name);
    if (kli_unlink(path) < 0) {
        copy_cstr(s->status, sizeof(s->status), "DELETE FAILED");
        return;
    }
    refresh_list(s);
    copy_cstr(s->status, sizeof(s->status), "DELETED");
}

static int handle_text_key(files_state_t *s, int key) {
    if (key == 27) {
        s->mode = FILES_MODE_NORMAL;
        copy_cstr(s->status, sizeof(s->status), "CANCELLED");
        return 1;
    }
    if (key == 8 || key == 127) {
        if (s->input_len > 0) {
            s->input_len--;
            s->input[s->input_len] = '\0';
        }
        return 1;
    }
    if (key == 13 || key == 10) {
        files_mode_t mode = s->mode;
        s->mode = FILES_MODE_NORMAL;
        if (mode == FILES_MODE_NEW) {
            create_file(s);
        } else {
            rename_file(s);
        }
        return 1;
    }
    if (key >= 32 && key <= 126 && s->input_len + 1 < NAME_CAP) {
        s->input[s->input_len++] = uppercase_83_char((char)key);
        s->input[s->input_len] = '\0';
        return 1;
    }
    return 0;
}

static int handle_key(files_state_t *s, int key) {
    if (key == 17) {
        (void)gui_window_destroy(s->wid);
        kli_exit(0);
        for (;;) {
            (void)kli_yield();
        }
    }

    if (s->mode == FILES_MODE_NEW || s->mode == FILES_MODE_RENAME) {
        return handle_text_key(s, key);
    }

    if (s->mode == FILES_MODE_DELETE) {
        s->mode = FILES_MODE_NORMAL;
        if (key == 'y' || key == 'Y') {
            delete_file(s);
        } else {
            copy_cstr(s->status, sizeof(s->status), "CANCELLED");
        }
        return 1;
    }

    if (key == INPUT_KEY_UP) {
        if (s->selected > 0) {
            s->selected--;
        }
        return 1;
    }
    if (key == INPUT_KEY_DOWN) {
        if (s->selected + 1 < s->count) {
            s->selected++;
        }
        return 1;
    }
    if (key == 13 || key == 10) {
        open_selected(s);
        return 1;
    }
    if (key == 'n' || key == 'N') {
        start_text_mode(s, FILES_MODE_NEW);
        return 1;
    }
    if (key == 'r' || key == 'R') {
        if (selected_name(s) == 0) {
            copy_cstr(s->status, sizeof(s->status), "NO SELECTION");
        } else {
            start_text_mode(s, FILES_MODE_RENAME);
        }
        return 1;
    }
    if (key == 'd' || key == 'D') {
        if (selected_name(s) == 0) {
            copy_cstr(s->status, sizeof(s->status), "NO SELECTION");
        } else {
            s->mode = FILES_MODE_DELETE;
            copy_cstr(s->status, sizeof(s->status), "CONFIRM DELETE");
        }
        return 1;
    }
    return 0;
}

static void files_state_init(files_state_t *s) {
    s->wid = 0;
    s->selected = 0;
    s->count = 0;
    s->mode = FILES_MODE_NORMAL;
    s->input[0] = '\0';
    s->input_len = 0;
    copy_cstr(s->status, sizeof(s->status), "STARTING");
    clear_entries(s);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    long state_addr = kli_mmap(0, sizeof(files_state_t), 0);
    if (state_addr < 0) {
        kli_write_cstr(1, "files: state mmap failed\n");
        return 1;
    }
    files_state_t *s = (files_state_t *)(uintptr_t)state_addr;
    files_state_init(s);

    kli_write_cstr(1, "files: starting\n");

    s->wid = gui_window_create(WIN_X, WIN_Y, WIN_W, WIN_H,
                               COLOR_BG, COLOR_BORDER, "files");
    if (s->wid < 0) {
        kli_write_cstr(1, "files: window create failed\n");
        return 1;
    }
    (void)gui_window_set_title(s->wid, "files", TITLE_BAR_H);

    refresh_list(s);
    redraw(s);
    kli_write_cstr(1, "files: ready\n");

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
                    if (handle_key(s, s->events[i].data1) != 0) {
                        dirty = 1;
                    }
                }
            }
            if (dirty != 0) {
                redraw(s);
            }
        } else {
            (void)kli_yield();
        }
    }
}
