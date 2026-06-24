#include "kernel/syscall.h"

#include <stdint.h>

#include "drivers/board.h"
#include "input/input.h"
#include "kernel/console.h"
#include "kernel/exceptions.h"
#include "kernel/gui.h"
#include "kernel/ipc.h"
#include "kernel/mm/pmm.h"
#include "kernel/print.h"
#include "kernel/process.h"
#include "kernel/sched/sched.h"
#include "kernel/syscall_numbers.h"
#include "kernel/timer/timer.h"
#include "kernel/panel_boot.h"
#include "kernel/user_vm.h"
#include "kernel/vfs.h"
#include "uart/pl011.h"

#define FD_STDIN  0ULL
#define FD_STDOUT 1ULL
#define FD_STDERR 2ULL
#define FD_FILE_BASE 3ULL

#define ERR_NOENT (-3LL)
#define ERR_BADF  (-5LL)
#define ERR_INVAL (-7LL)
#define ERR_AGAIN (-11LL)

#define SPSR_EL1H_MASKED 0x3c5ULL

typedef struct {
    uint32_t pid;
    uint32_t state;
    char name[16];
} syscall_proc_entry_t;

static int user_range_contains(uint64_t ptr, uint64_t len) {
    return process_user_range_contains(process_current(), ptr, len);
}

static int copy_user_cstr(uint64_t ptr, char *out, uint64_t capacity) {
    const char *src = (const char *)(uintptr_t)ptr;

    if (ptr == 0 || out == 0 || capacity == 0) {
        return -1;
    }

    for (uint64_t i = 0; i < capacity; i++) {
        if (!user_range_contains(ptr + i, 1)) {
            return -1;
        }

        out[i] = src[i];
        if (out[i] == '\0') {
            return 0;
        }
    }

    return -1;
}

static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len) {
    const char *text = (const char *)(uintptr_t)buf;
    uint64_t bytes_written = 0;

    if (!user_range_contains(buf, len)) {
        return ERR_INVAL;
    }

    if (fd >= (uint64_t)FD_FILE_BASE) {
        if (vfs_write_fd((int64_t)fd - FD_FILE_BASE,
                         (const uint8_t *)(uintptr_t)buf, len,
                         &bytes_written) != 0) {
            return ERR_BADF;
        }
        return (int64_t)bytes_written;
    }

    if (fd != FD_STDOUT && fd != FD_STDERR) {
        return ERR_BADF;
    }

    for (uint64_t i = 0; i < len; i++) {
        uart_putc(text[i]);
    }

    return (int64_t)len;
}

static int64_t sys_open(uint64_t path_ptr, uint64_t flags) {
    char path[VFS_MAX_PATH];
    int fd;

    if (flags > VFS_O_RDWR ||
        copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return ERR_INVAL;
    }

    fd = vfs_open_flags(path, (uint32_t)flags);
    if (fd < 0) {
        return ERR_NOENT;
    }

    return (int64_t)fd + FD_FILE_BASE;
}

static int64_t sys_spawn(uint64_t path_ptr, uint64_t entry_index) {
    char path[VFS_MAX_PATH];
    int pid;

    if (entry_index > 0xffffffffULL ||
        copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return ERR_INVAL;
    }

    pid = kolibri_spawn_vfs(path, (uint32_t)entry_index, 0, 0);
    if (pid < 0) {
        return ERR_NOENT;
    }

    return pid;
}

static int64_t sys_spawn_argv(uint64_t path_ptr, uint64_t entry_index,
                              uint64_t argv_ptr, uint64_t argc) {
    char path[VFS_MAX_PATH];
    int pid;

    if (entry_index > 0xffffffffULL || argc > 0xffffffffULL ||
        copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return ERR_INVAL;
    }

    /*
     * argv_ptr must point at `argc` uint64_t entries inside the
     * caller's registered user regions when argc > 0. The strings
     * they reference are validated by kolibri_spawn_vfs (it walks
     * each one until '\0'). argc == 0 with argv_ptr == 0 is the
     * "no argv" path and is always accepted; any other combination
     * of argc and argv_ptr is rejected.
     */
    if (argc == 0) {
        if (argv_ptr != 0) {
            return ERR_INVAL;
        }
    } else {
        if (argv_ptr == 0 ||
            !user_range_contains(argv_ptr, argc * sizeof(uint64_t))) {
            return ERR_INVAL;
        }
    }

    pid = kolibri_spawn_vfs(path, (uint32_t)entry_index,
                              (const uint64_t *)(uintptr_t)argv_ptr,
                              (uint32_t)argc);
    if (pid < 0) {
        return ERR_NOENT;
    }

    return pid;
}

static int64_t sys_wait(uint64_t pid) {
    uint64_t exit_code = 0;
    process_t *process;

    if (pid == 0 || pid > 0xffffffffULL) {
        return ERR_INVAL;
    }

    process = process_find((uint32_t)pid);
    if (process == 0) {
        return ERR_NOENT;
    }

    if (process->state != PROCESS_ZOMBIE) {
        return ERR_AGAIN;
    }

    if (process_wait_zombie((uint32_t)pid, &exit_code) != 0) {
        return ERR_INVAL;
    }

    return (int64_t)exit_code;
}

static int64_t sys_kill(uint64_t pid) {
    if (pid == 0 || pid > 0xffffffffULL) {
        return ERR_INVAL;
    }

    if (process_find((uint32_t)pid) == 0) {
        return ERR_NOENT;
    }

    if (process_kill((uint32_t)pid, 0x80ULL) != 0) {
        return ERR_INVAL;
    }

    return 0;
}

static int64_t sys_close(uint64_t fd) {
    if (fd < (uint64_t)FD_FILE_BASE) {
        return ERR_BADF;
    }

    if (vfs_close((int64_t)fd - FD_FILE_BASE) != 0) {
        return ERR_BADF;
    }

    return 0;
}

static int64_t sys_seek(uint64_t fd, uint64_t offset, uint64_t whence) {
    if (fd < (uint64_t)FD_FILE_BASE || whence != 0) {
        return ERR_INVAL;
    }

    if (vfs_seek((int64_t)fd - FD_FILE_BASE, offset) != 0) {
        return ERR_BADF;
    }

    return (int64_t)offset;
}

static int64_t sys_read(uint64_t fd, uint64_t buf, uint64_t len) {
    uint64_t bytes_read = 0;

    if (fd == FD_STDIN) {
        uint8_t *out = (uint8_t *)(uintptr_t)buf;
        int c;

        if (!user_range_contains(buf, len)) {
            return ERR_INVAL;
        }

        if (len == 0) {
            return 0;
        }

        c = input_queue_poll_char();
        if (c < 0) {
            return ERR_AGAIN;
        }

        out[0] = (uint8_t)c;
        return 1;
    }

    if (fd < (uint64_t)FD_FILE_BASE) {
        return ERR_BADF;
    }

    if (!user_range_contains(buf, len)) {
        return ERR_INVAL;
    }

    if (vfs_read_fd((int64_t)fd - FD_FILE_BASE, (uint8_t *)(uintptr_t)buf,
                    len, &bytes_read) != 0) {
        return ERR_BADF;
    }

    return (int64_t)bytes_read;
}

static int64_t sys_stat(uint64_t path_ptr, uint64_t stat_ptr) {
    char path[VFS_MAX_PATH];
    vfs_stat_t stat;

    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0 ||
        !user_range_contains(stat_ptr, sizeof(stat))) {
        return ERR_INVAL;
    }

    if (vfs_stat(path, &stat) != 0) {
        return ERR_NOENT;
    }

    *(vfs_stat_t *)(uintptr_t)stat_ptr = stat;
    return 0;
}

static int64_t sys_readdir(uint64_t path_ptr, uint64_t buf, uint64_t len) {
    char path[VFS_MAX_PATH];
    uint64_t bytes_written = 0;

    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0 ||
        !user_range_contains(buf, len)) {
        return ERR_INVAL;
    }

    if (vfs_list(path, (uint8_t *)(uintptr_t)buf, len, &bytes_written) != 0) {
        return ERR_NOENT;
    }

    return (int64_t)bytes_written;
}

static int64_t sys_unlink(uint64_t path_ptr) {
    char path[VFS_MAX_PATH];

    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return ERR_INVAL;
    }
    if (vfs_unlink(path) != 0) {
        return ERR_NOENT;
    }
    return 0;
}

static int64_t sys_rename(uint64_t old_ptr, uint64_t new_ptr) {
    char old_path[VFS_MAX_PATH];
    char new_path[VFS_MAX_PATH];

    if (copy_user_cstr(old_ptr, old_path, sizeof(old_path)) != 0 ||
        copy_user_cstr(new_ptr, new_path, sizeof(new_path)) != 0) {
        return ERR_INVAL;
    }
    if (vfs_rename(old_path, new_path) != 0) {
        return ERR_NOENT;
    }
    return 0;
}

static int64_t sys_meminfo(uint64_t info_ptr) {
    uint64_t *info = (uint64_t *)(uintptr_t)info_ptr;

    if (!user_range_contains(info_ptr, 2U * sizeof(uint64_t))) {
        return ERR_INVAL;
    }

    info[0] = pmm_total_count();
    info[1] = pmm_free_count();
    return 0;
}

static int64_t sys_timeinfo(uint64_t info_ptr) {
    uint64_t *info = (uint64_t *)(uintptr_t)info_ptr;

    if (!user_range_contains(info_ptr, 3U * sizeof(uint64_t))) {
        return ERR_INVAL;
    }

    info[0] = timer_ticks();
    info[1] = sched_ticks();
    info[2] = sched_quantums();
    return 0;
}

static int64_t sys_proclist(uint64_t entries_ptr, uint64_t max_entries) {
    syscall_proc_entry_t *entries =
        (syscall_proc_entry_t *)(uintptr_t)entries_ptr;
    uint64_t written = 0;

    if (max_entries == 0) {
        return 0;
    }

    if (max_entries > PROCESS_MAX_PROCESSES ||
        !user_range_contains(entries_ptr,
                             max_entries * sizeof(syscall_proc_entry_t))) {
        return ERR_INVAL;
    }

    for (uint32_t i = 0; i < PROCESS_MAX_PROCESSES && written < max_entries; i++) {
        const process_t *process = process_at(i);
        syscall_proc_entry_t *entry;
        const char *name;
        uint32_t j;

        if (process == 0) {
            continue;
        }

        entry = &entries[written];
        entry->pid = process->pid;
        entry->state = (uint32_t)process->state;
        name = process->name != 0 ? process->name : "";

        for (j = 0; j + 1U < sizeof(entry->name) && name[j] != '\0'; j++) {
            entry->name[j] = name[j];
        }
        entry->name[j] = '\0';
        for (j++; j < sizeof(entry->name); j++) {
            entry->name[j] = '\0';
        }

        written++;
    }

    return (int64_t)written;
}

static int64_t sys_munmap(process_t *process, uint64_t addr, uint64_t size) {
    return user_vm_unmap_anonymous(process, addr, size);
}

static int64_t sys_mmap(process_t *process, uint64_t hint, uint64_t size, uint64_t flags) {
    return user_vm_map_anonymous(process, hint, size, flags);
}

static int64_t sys_ipc_send(process_t *process, uint64_t target_pid,
                            uint64_t buf, uint64_t len) {
    if (process == 0 || target_pid == 0 || len == 0 ||
        target_pid > 0xffffffffULL || len > IPC_MAX_MESSAGE_SIZE ||
        !user_range_contains(buf, len)) {
        return ERR_INVAL;
    }

    if (ipc_send(process->pid, (uint32_t)target_pid,
                 (const uint8_t *)(uintptr_t)buf, (uint32_t)len) != 0) {
        return ERR_AGAIN;
    }

    return (int64_t)len;
}

static int64_t sys_ipc_recv(process_t *process, uint64_t buf,
                            uint64_t capacity) {
    ipc_message_t message;
    uint8_t *out = (uint8_t *)(uintptr_t)buf;

    if (process == 0 || buf == 0 || capacity != IPC_MAX_MESSAGE_SIZE ||
        !user_range_contains(buf, capacity)) {
        return ERR_INVAL;
    }

    if (ipc_recv(process->pid, &message) != 0) {
        return ERR_AGAIN;
    }

    if (message.size > capacity) {
        return ERR_INVAL;
    }

    for (uint32_t i = 0; i < message.size; i++) {
        out[i] = message.data[i];
    }

    return (int64_t)message.size;
}

static int sys_yield_process(exception_frame_t *frame) {
    process_t *current = process_current();
    process_t *next = process_next_runnable(current);

    if (current == 0 || next == 0) {
        return 0;
    }

    if (current->state == PROCESS_RUNNING) {
        current->state = PROCESS_READY;
    }

    next->state = PROCESS_RUNNING;
    process_set_current(next);
    process_activate_context(next, frame);

    return 1;
}

static int64_t sys_window_create(process_t *process, uint64_t x, uint64_t y,
                                 uint64_t w, uint64_t h, uint64_t bg,
                                 uint64_t border, uint64_t title_ptr) {
    char title[GUI_TITLE_LEN];
    uint32_t window_id = GUI_NO_WINDOW;

    if (process == 0 || x > 0xffffffffULL || y > 0xffffffffULL ||
        w > 0xffffffffULL || h > 0xffffffffULL || w < 8 || h < 8 ||
        bg > 0xffffffffULL || border > 0xffffffffULL) {
        return ERR_INVAL;
    }

    for (uint32_t i = 0; i < GUI_TITLE_LEN; i++) {
        title[i] = '\0';
    }
    if (title_ptr != 0) {
        if (copy_user_cstr(title_ptr, title, GUI_TITLE_LEN) != 0) {
            return ERR_INVAL;
        }
    }

    if (gui_desktop() == 0) {
        return ERR_AGAIN;
    }

    if (gui_create_window_for_pid(gui_desktop(), process->pid,
                                  (uint32_t)x, (uint32_t)y, (uint32_t)w,
                                  (uint32_t)h, (uint32_t)bg,
                                  (uint32_t)border, title,
                                  &window_id) != 0) {
        return ERR_AGAIN;
    }
    /* New window must show on the next compositor pass. */
    gui_request_redraw();
    return (int64_t)window_id;
}

static int64_t sys_window_destroy(process_t *process, uint64_t window_id) {
    if (process == 0 || window_id >= GUI_MAX_WINDOWS) {
        return ERR_INVAL;
    }
    gui_window_t *window = &gui_desktop()->windows[window_id];
    if (window->used == 0) {
        return ERR_NOENT;
    }
    if (window->owner_pid != process->pid) {
        return ERR_BADF;
    }
    if (gui_destroy_window(gui_desktop(), (uint32_t)window_id) != 0) {
        return ERR_BADF;
    }
    /* Old window rectangle must be repainted by the compositor. */
    gui_request_redraw();
    return 0;
}

static int64_t sys_window_draw_text(process_t *process, uint64_t window_id,
                                    uint64_t x, uint64_t y, uint64_t color,
                                    uint64_t str_ptr) {
    char text[128];
    if (process == 0 || window_id >= GUI_MAX_WINDOWS) {
        return ERR_INVAL;
    }
    gui_window_t *window = &gui_desktop()->windows[window_id];
    if (window->used == 0 || window->owner_pid != process->pid) {
        return ERR_BADF;
    }
    if (copy_user_cstr(str_ptr, text, sizeof(text)) != 0) {
        return ERR_INVAL;
    }
    if (gui_window_draw_text(gui_desktop(), (uint32_t)window_id,
                             (int32_t)x, (int32_t)y, text,
                             (uint32_t)color) != 0) {
        return ERR_BADF;
    }
    return 0;
}

static int64_t sys_window_draw_rect(process_t *process, uint64_t window_id,
                                    uint64_t x, uint64_t y, uint64_t w,
                                    uint64_t h, uint64_t color) {
    if (process == 0 || window_id >= GUI_MAX_WINDOWS ||
        x > 0x7fffffffULL || y > 0x7fffffffULL ||
        w > 0xffffffffULL || h > 0xffffffffULL) {
        return ERR_INVAL;
    }
    gui_window_t *window = &gui_desktop()->windows[window_id];
    if (window->used == 0 || window->owner_pid != process->pid) {
        return ERR_BADF;
    }
    if (gui_window_draw_rect(gui_desktop(), (uint32_t)window_id,
                             (int32_t)x, (int32_t)y, (uint32_t)w,
                             (uint32_t)h, (uint32_t)color) != 0) {
        return ERR_BADF;
    }
    return 0;
}

static int64_t sys_window_set_title(process_t *process, uint64_t window_id,
                                    uint64_t title_ptr, uint64_t title_h) {
    char title[GUI_TITLE_LEN];
    if (process == 0 || window_id >= GUI_MAX_WINDOWS) {
        return ERR_INVAL;
    }
    gui_window_t *window = &gui_desktop()->windows[window_id];
    if (window->used == 0 || window->owner_pid != process->pid) {
        return ERR_BADF;
    }
    if (copy_user_cstr(title_ptr, title, GUI_TITLE_LEN) != 0) {
        return ERR_INVAL;
    }
    if (gui_set_window_title(gui_desktop(), (uint32_t)window_id,
                             title) != 0) {
        return ERR_BADF;
    }
    /* Title text change must show on the next compositor pass. */
    gui_request_redraw();
    /* title_h is an optional fourth argument. Older apps leave x2 unset,
     * so we keep the default of 0 (no kernel title bar) for ABI
     * compatibility. The kernel validates title_h against the window
     * height before applying. */
    if (title_h > 0 &&
        gui_set_window_title_bar(gui_desktop(), (uint32_t)window_id,
                                 (uint32_t)title_h) != 0) {
        return ERR_INVAL;
    }
    return 0;
}

static int64_t sys_window_redraw(process_t *process, uint64_t window_id) {
    if (process == 0 || window_id >= GUI_MAX_WINDOWS) {
        return ERR_INVAL;
    }
    gui_window_t *window = &gui_desktop()->windows[window_id];
    if (window->used == 0 || window->owner_pid != process->pid) {
        return ERR_BADF;
    }
    /* Mark the desktop dirty so the kernel redraws on next tick. */
    gui_request_redraw();
    return 0;
}

static int64_t sys_window_focus(process_t *process, uint64_t window_id) {
    if (process == 0 || window_id >= GUI_MAX_WINDOWS) {
        return ERR_INVAL;
    }
    gui_window_t *window = &gui_desktop()->windows[window_id];
    if (window->used == 0) {
        return ERR_NOENT;
    }
    /* Unlike the owner-only draw/destroy syscalls, sys_window_focus is
     * deliberately callable by any process. The desktop taskbar (a
     * different pid from the app owners) needs to raise windows it did
     * not create when the user clicks a running-app entry. */
    if (gui_focus_window(gui_desktop(), (uint32_t)window_id) != 0) {
        return ERR_INVAL;
    }
    gui_request_redraw();
    return 0;
}

static int64_t sys_window_for_pid(process_t *process, uint64_t owner_pid,
                                  uint64_t index) {
    if (process == 0 || owner_pid > UINT32_MAX || index > GUI_MAX_WINDOWS) {
        return ERR_INVAL;
    }
    uint32_t window_id = gui_window_for_pid(gui_desktop(),
                                            (uint32_t)owner_pid,
                                            (uint32_t)index);
    if (window_id == GUI_NO_WINDOW) {
        return ERR_NOENT;
    }
    return (int64_t)window_id;
}

static int64_t sys_cursor_set_shape(process_t *process, uint64_t shape) {
    if (process == 0 || shape > UINT32_MAX) {
        return ERR_INVAL;
    }
    if (gui_set_cursor_shape(gui_desktop(), (uint32_t)shape) != 0) {
        return ERR_INVAL;
    }
    gui_request_redraw();
    return 0;
}

/*
 * sys_window_flush: push a content-local damage rectangle for the given
 * window. The compositor converts it to framebuffer coordinates and
 * merges it with other pending damage, so a flurry of small draws
 * coalesces into one partial redraw. Used by apps that draw into their
 * own backing buffer through a path the kernel does not see (for
 * example, blitting an image they built locally) and need to tell the
 * compositor which pixels changed.
 */
static int64_t sys_window_flush(process_t *process, uint64_t window_id,
                                uint64_t x, uint64_t y, uint64_t w,
                                uint64_t h) {
    if (process == 0 || window_id >= GUI_MAX_WINDOWS ||
        x > 0x7fffffffULL || y > 0x7fffffffULL ||
        w > 0xffffffffULL || h > 0xffffffffULL) {
        return ERR_INVAL;
    }
    gui_desktop_t *desktop = gui_desktop();
    if (desktop == 0) {
        return ERR_AGAIN;
    }
    gui_window_t *window = &desktop->windows[window_id];
    if (window->used == 0 || window->owner_pid != process->pid) {
        return ERR_BADF;
    }
    if (w == 0 || h == 0) {
        return 0;
    }
    /* Convert content-local coords to framebuffer coords: the content
     * area sits below the kernel-drawn title bar. */
    gui_damage_add(desktop,
                   (int32_t)window->x + (int32_t)x,
                   (int32_t)window->y + (int32_t)window->title_h + (int32_t)y,
                   (int32_t)w, (int32_t)h);
    return 0;
}

/*
 * sys_window_get_bounds: write the window's current (x, y, w, h) into
 * the user buffer as four uint32_t values. out_ptr must point at a
 * 16-byte region registered with the caller. Only the owning process
 * may read another process's window bounds.
 */
static int64_t sys_window_get_bounds(process_t *process, uint64_t window_id,
                                    uint64_t out_ptr) {
    gui_desktop_t *desktop;
    gui_window_t *window;
    uint32_t *out = (uint32_t *)(uintptr_t)out_ptr;

    if (process == 0 || window_id >= GUI_MAX_WINDOWS || out_ptr == 0) {
        return ERR_INVAL;
    }
    if (!user_range_contains(out_ptr, sizeof(uint32_t) * 4U)) {
        return ERR_INVAL;
    }
    desktop = gui_desktop();
    if (desktop == 0) {
        return ERR_AGAIN;
    }
    window = &desktop->windows[window_id];
    if (window->used == 0) {
        return ERR_NOENT;
    }
    if (window->owner_pid != process->pid) {
        return ERR_BADF;
    }
    out[0] = window->x;
    out[1] = window->y;
    out[2] = window->w;
    out[3] = window->h;
    return 0;
}

/*
 * sys_window_set_bounds: move and/or resize the window in one step.
 * If (w, h) changes relative to the current window, the kernel
 * reallocates the per-window backing buffer (cleared to bg_color)
 * and pushes GUI_EVENT_RESIZE onto the owner's event queue so the
 * owner can rebuild its layout before the next redraw.
 *
 * Only the owning process may move or resize its window. Bounds are
 * validated against the desktop framebuffer; out-of-bounds moves
 * fail with ERR_INVAL without touching the window.
 */
static int64_t sys_window_set_bounds(process_t *process, uint64_t window_id,
                                    uint64_t x, uint64_t y, uint64_t w,
                                    uint64_t h) {
    if (process == 0 || window_id >= GUI_MAX_WINDOWS ||
        x > 0x7fffffffU || y > 0x7fffffffU ||
        w > 0xffffffffU || h > 0xffffffffU) {
        return ERR_INVAL;
    }
    gui_desktop_t *desktop = gui_desktop();
    if (desktop == 0) {
        return ERR_AGAIN;
    }
    gui_window_t *window = &desktop->windows[window_id];
    if (window->used == 0) {
        return ERR_NOENT;
    }
    if (window->owner_pid != process->pid) {
        return ERR_BADF;
    }
    if (gui_resize_window(desktop, (uint32_t)window_id, (uint32_t)x,
                          (uint32_t)y, (uint32_t)w, (uint32_t)h) != 0) {
        return ERR_INVAL;
    }
    return 0;
}

static int64_t sys_window_event(process_t *process, exception_frame_t *frame,
                               uint64_t window_id, uint64_t buf_ptr,
                               uint64_t buf_count) {
    uint32_t *out = (uint32_t *)(uintptr_t)buf_ptr;
    if (process == 0 || window_id >= GUI_MAX_WINDOWS ||
        buf_count == 0 || buf_count > 64) {
        return ERR_INVAL;
    }
    if (!user_range_contains(buf_ptr, buf_count * 3U * sizeof(uint32_t))) {
        return ERR_INVAL;
    }
    gui_window_t *window = &gui_desktop()->windows[window_id];
    if (window->used == 0 || window->owner_pid != process->pid) {
        return ERR_BADF;
    }

    uint64_t yielded = 0;
    while (window->event_count == 0) {
        sys_yield_process(frame);
        yielded++;
        if (yielded > 256) {
            /* Avoid spinning forever; yield back without an event. */
            return ERR_AGAIN;
        }
    }

    uint64_t n = 0;
    while (n < buf_count && window->event_count > 0) {
        gui_event_t ev;
        if (gui_window_pop_event(window, &ev) != 0) {
            break;
        }
        out[n * 3U + 0U] = ev.type;
        out[n * 3U + 1U] = (uint32_t)ev.data1;
        out[n * 3U + 2U] = (uint32_t)ev.data2;
        n++;
    }
    return (int64_t)n;
}

static void sys_exit(exception_frame_t *frame, uint64_t code) {
    process_t *current = process_current();
    process_t *next;

    process_mark_exited(current, code);

    uart_puts("USER exit: ");
    print_hex64(code);
    uart_puts("\n");

    next = process_next_runnable(current);
    if (next != 0) {
        next->state = PROCESS_RUNNING;
        process_set_current(next);
        process_activate_context(next, frame);
        return;
    }

    frame->x[0] = code;
    frame->elr = el0_return_address();
    frame->spsr = SPSR_EL1H_MASKED;
}

void syscall_dispatch(exception_frame_t *frame) {
    process_t *current = process_current();

    // Drain any pending input before handling this SVC, except when the
    // caller is actively reading stdin. That lets legacy/debug serial reads
    // keep their bytes while still giving the kernel k> console and focused
    // GUI windows a chance to process input while an EL0 process holds the CPU.
    // uart_pump_input drains the UART data register directly so piped
    // QEMU input is captured even when the PL011 RX interrupt is
    // masked or pending.
    uart_pump_input();
    input_uart_poll();
    board_virtio_input_poll();
    if (frame->x[8] != SYS_READ) {
        input_event_t drain_event;
        while (input_queue_poll(&drain_event) == 0) {
            if (drain_event.type == INPUT_EVENT_KEY_PRESS) {
                char c = (char)drain_event.data.key.key;
                console_poll_char(c);
                (void)gui_handle_input(&drain_event);
            } else {
                // Mouse move and button events get dispatched straight to
                // the GUI so the panel can react to 'mouse x y' and
                // 'click x y' commands issued from the serial console.
                (void)gui_handle_input(&drain_event);
            }
        }
    }

    if (current != 0) {
        process_save_context(current, frame->x, frame->elr, frame->spsr,
                             frame->sp_el0);
    }

    switch (frame->x[8]) {
    case SYS_EXIT:
        sys_exit(frame, frame->x[0]);
        break;
    case SYS_YIELD:
        if (!sys_yield_process(frame)) {
            sched_yield();
            frame->x[0] = 0;
        }
        break;
    case SYS_GETPID:
        if (current == 0) {
            frame->x[0] = (uint64_t)ERR_INVAL;
        } else {
            frame->x[0] = current->pid;
        }
        break;
    case SYS_SPAWN:
        frame->x[0] = (uint64_t)sys_spawn(frame->x[0], frame->x[1]);
        break;
    case SYS_SPAWN_ARGV:
        frame->x[0] = (uint64_t)sys_spawn_argv(frame->x[0], frame->x[1],
                                              frame->x[2], frame->x[3]);
        break;
    case SYS_WAIT:
        frame->x[0] = (uint64_t)sys_wait(frame->x[0]);
        break;
    case SYS_KILL:
        frame->x[0] = (uint64_t)sys_kill(frame->x[0]);
        break;
    case SYS_OPEN:
        frame->x[0] = (uint64_t)sys_open(frame->x[0], frame->x[1]);
        break;
    case SYS_CLOSE:
        frame->x[0] = (uint64_t)sys_close(frame->x[0]);
        break;
    case SYS_READ:
        frame->x[0] = (uint64_t)sys_read(frame->x[0], frame->x[1], frame->x[2]);
        break;
    case SYS_MMAP:
        frame->x[0] = (uint64_t)sys_mmap(current, frame->x[0], frame->x[1], frame->x[2]);
        break;
    case SYS_MUNMAP:
        frame->x[0] = (uint64_t)sys_munmap(current, frame->x[0], frame->x[1]);
        break;
    case SYS_WRITE:
        frame->x[0] = (uint64_t)sys_write(frame->x[0], frame->x[1], frame->x[2]);
        break;
    case SYS_SEEK:
        frame->x[0] = (uint64_t)sys_seek(frame->x[0], frame->x[1], frame->x[2]);
        break;
    case SYS_STAT:
        frame->x[0] = (uint64_t)sys_stat(frame->x[0], frame->x[1]);
        break;
    case SYS_READDIR:
        frame->x[0] = (uint64_t)sys_readdir(frame->x[0], frame->x[1],
                                            frame->x[2]);
        break;
    case SYS_UNLINK:
        frame->x[0] = (uint64_t)sys_unlink(frame->x[0]);
        break;
    case SYS_RENAME:
        frame->x[0] = (uint64_t)sys_rename(frame->x[0], frame->x[1]);
        break;
    case SYS_IPC_SEND:
        frame->x[0] = (uint64_t)sys_ipc_send(current, frame->x[0],
                                             frame->x[1], frame->x[2]);
        break;
    case SYS_IPC_RECV:
        frame->x[0] = (uint64_t)sys_ipc_recv(current, frame->x[0],
                                             frame->x[1]);
        break;
    case SYS_WINDOW_CREATE:
        frame->x[0] = (uint64_t)sys_window_create(current, frame->x[0],
                                                  frame->x[1], frame->x[2],
                                                  frame->x[3], frame->x[4],
                                                  frame->x[5], frame->x[6]);
        break;
    case SYS_WINDOW_DESTROY:
        frame->x[0] = (uint64_t)sys_window_destroy(current, frame->x[0]);
        break;
    case SYS_WINDOW_DRAW_TEXT:
        frame->x[0] = (uint64_t)sys_window_draw_text(current, frame->x[0],
                                                     frame->x[1], frame->x[2],
                                                     frame->x[3],
                                                     frame->x[4]);
        break;
    case SYS_WINDOW_DRAW_RECT:
        frame->x[0] = (uint64_t)sys_window_draw_rect(current, frame->x[0],
                                                     frame->x[1], frame->x[2],
                                                     frame->x[3], frame->x[4],
                                                     frame->x[5]);
        break;
    case SYS_WINDOW_EVENT:
        frame->x[0] = (uint64_t)sys_window_event(current, frame, frame->x[0],
                                                frame->x[1], frame->x[2]);
        break;
    case SYS_WINDOW_SET_TITLE:
        frame->x[0] = (uint64_t)sys_window_set_title(current, frame->x[0],
                                                     frame->x[1],
                                                     frame->x[2]);
        break;
    case SYS_WINDOW_REDRAW:
        frame->x[0] = (uint64_t)sys_window_redraw(current, frame->x[0]);
        break;
    case SYS_WINDOW_FOCUS:
        frame->x[0] = (uint64_t)sys_window_focus(current, frame->x[0]);
        break;
    case SYS_WINDOW_FOR_PID:
        frame->x[0] = (uint64_t)sys_window_for_pid(current, frame->x[0],
                                                   frame->x[1]);
        break;
    case SYS_CURSOR_SET_SHAPE:
        frame->x[0] = (uint64_t)sys_cursor_set_shape(current, frame->x[0]);
        break;
    case SYS_WINDOW_FLUSH:
        frame->x[0] = (uint64_t)sys_window_flush(current, frame->x[0],
                                                  frame->x[1], frame->x[2],
                                                  frame->x[3], frame->x[4]);
        break;
    case SYS_WINDOW_GET_BOUNDS:
        frame->x[0] = (uint64_t)sys_window_get_bounds(current, frame->x[0],
                                                      frame->x[1]);
        break;
    case SYS_WINDOW_SET_BOUNDS:
        frame->x[0] = (uint64_t)sys_window_set_bounds(current, frame->x[0],
                                                      frame->x[1],
                                                      frame->x[2], frame->x[3],
                                                      frame->x[4]);
        break;
    case SYS_TIMEINFO:
        frame->x[0] = (uint64_t)sys_timeinfo(frame->x[0]);
        break;
    case SYS_MEMINFO:
        frame->x[0] = (uint64_t)sys_meminfo(frame->x[0]);
        break;
    case SYS_PROCLIST:
        frame->x[0] = (uint64_t)sys_proclist(frame->x[0], frame->x[1]);
        break;
    default:
        frame->x[0] = (uint64_t)ERR_INVAL;
        break;
    }

    process_t *after = process_current();
    if (after != 0) {
        after->regs[0] = frame->x[0];
        after->pc = frame->elr;
        after->pstate = frame->spsr;
        after->sp = frame->sp_el0;
    }
}
