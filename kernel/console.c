#include "kernel/console.h"

#include <stdint.h>

#include "drivers/input/input.h"
#include "kernel/gui.h"
#include "kernel/init_status.h"
#include "kernel/mm/pmm.h"
#include "kernel/print.h"
#include "kernel/process.h"
#include "kernel/sched/sched.h"
#include "kernel/timer/timer.h"
#include "uart/pl011.h"

#define CONSOLE_LINE_MAX 64U

/*
 * Interactive kernel console.
 *
 * The console owns serial line editing and command dispatch. Plain formatting
 * helpers stay in print.c so boot logs and interactive commands share numeric
 * output without sharing command-line state.
 */

static char g_line[CONSOLE_LINE_MAX];
static uint32_t g_line_len;
static dtb_memory_t g_memory;
static int g_have_memory;
static int g_interactive;
/*
 * Last known absolute cursor position, used to synthesize relative mouse_move
 * events when the user drives the mouse from the serial console. Initialized
 * on the first cursor query.
 */
static int g_cursor_known;
static int32_t g_cursor_x;
static int32_t g_cursor_y;

typedef struct {
    uint8_t name;
    uint8_t help;
    uint8_t id;
} k_command_t;

#define COMMAND_NAME_HELP   "help"
#define COMMAND_NAME_MEM    "mem"
#define COMMAND_NAME_PS     "ps"
#define COMMAND_NAME_TICKS  "ticks"
#define COMMAND_NAME_STATUS "status"
#define COMMAND_NAME_MOUSE  "mouse"
#define COMMAND_NAME_CLICK  "click"
#define COMMAND_NAME_KEY    "key"
#define COMMAND_HELP_EMPTY  ""
#define COMMAND_HELP_XY     "<x> <y>"
#define COMMAND_HELP_CHAR   "<char>"

enum {
    COMMAND_TEXT_HELP = 0,
    COMMAND_TEXT_MEM = COMMAND_TEXT_HELP + sizeof(COMMAND_NAME_HELP),
    COMMAND_TEXT_PS = COMMAND_TEXT_MEM + sizeof(COMMAND_NAME_MEM),
    COMMAND_TEXT_TICKS = COMMAND_TEXT_PS + sizeof(COMMAND_NAME_PS),
    COMMAND_TEXT_STATUS = COMMAND_TEXT_TICKS + sizeof(COMMAND_NAME_TICKS),
    COMMAND_TEXT_MOUSE = COMMAND_TEXT_STATUS + sizeof(COMMAND_NAME_STATUS),
    COMMAND_TEXT_CLICK = COMMAND_TEXT_MOUSE + sizeof(COMMAND_NAME_MOUSE),
    COMMAND_TEXT_KEY = COMMAND_TEXT_CLICK + sizeof(COMMAND_NAME_CLICK),
    COMMAND_TEXT_EMPTY = COMMAND_TEXT_KEY + sizeof(COMMAND_NAME_KEY),
    COMMAND_TEXT_XY = COMMAND_TEXT_EMPTY + sizeof(COMMAND_HELP_EMPTY),
    COMMAND_TEXT_CHAR = COMMAND_TEXT_XY + sizeof(COMMAND_HELP_XY),
};

#define COMMAND_ID_HELP   0U
#define COMMAND_ID_MEM    1U
#define COMMAND_ID_PS     2U
#define COMMAND_ID_TICKS  3U
#define COMMAND_ID_STATUS 4U
#define COMMAND_ID_MOUSE  5U
#define COMMAND_ID_CLICK  6U
#define COMMAND_ID_KEY    7U

static const char g_command_text[] =
    COMMAND_NAME_HELP "\0"
    COMMAND_NAME_MEM "\0"
    COMMAND_NAME_PS "\0"
    COMMAND_NAME_TICKS "\0"
    COMMAND_NAME_STATUS "\0"
    COMMAND_NAME_MOUSE "\0"
    COMMAND_NAME_CLICK "\0"
    COMMAND_NAME_KEY "\0"
    COMMAND_HELP_EMPTY "\0"
    COMMAND_HELP_XY "\0"
    COMMAND_HELP_CHAR;

_Static_assert(sizeof(g_command_text) <= 256U,
               "command text offsets must fit in uint8_t");

static const k_command_t g_commands[] = {
    {COMMAND_TEXT_HELP, COMMAND_TEXT_EMPTY, COMMAND_ID_HELP},
    {COMMAND_TEXT_MEM, COMMAND_TEXT_EMPTY, COMMAND_ID_MEM},
    {COMMAND_TEXT_PS, COMMAND_TEXT_EMPTY, COMMAND_ID_PS},
    {COMMAND_TEXT_TICKS, COMMAND_TEXT_EMPTY, COMMAND_ID_TICKS},
    {COMMAND_TEXT_STATUS, COMMAND_TEXT_EMPTY, COMMAND_ID_STATUS},
    {COMMAND_TEXT_MOUSE, COMMAND_TEXT_XY, COMMAND_ID_MOUSE},
    {COMMAND_TEXT_CLICK, COMMAND_TEXT_XY, COMMAND_ID_CLICK},
    {COMMAND_TEXT_KEY, COMMAND_TEXT_CHAR, COMMAND_ID_KEY},
};

static const uint32_t g_command_count =
    (uint32_t)(sizeof(g_commands) / sizeof(g_commands[0]));

static const char *command_text(uint8_t offset) {
    return &g_command_text[offset];
}

static int command_matches(const char *line, const char *name,
                           const char **out_args) {
    while (*name != '\0') {
        if (*line != *name) {
            return 0;
        }
        line++;
        name++;
    }

    if (*line == '\0') {
        *out_args = line;
        return 1;
    }
    if (*line == ' ') {
        *out_args = line + 1;
        return 1;
    }
    return 0;
}

static void print_prompt(void) {
    uart_puts("k> ");
}

static void print_usage(const k_command_t *command) {
    const char *help = command_text(command->help);

    uart_puts("usage: ");
    uart_puts(command_text(command->name));
    if (help[0] != '\0') {
        uart_puts(" ");
        uart_puts(help);
    }
    uart_puts("\n");
}

static void run_help(void) {
    for (uint32_t i = 0; i < g_command_count; i++) {
        print_usage(&g_commands[i]);
    }
}

static void run_mem(void) {
    if (g_have_memory == 0) {
        uart_puts("mem: unavailable\n");
        return;
    }

    uart_puts("mem base=");
    print_hex64(g_memory.base);
    uart_puts(" size=");
    print_hex64(g_memory.size);
    uart_puts(" free_pages=");
    print_dec64(pmm_free_count());
    uart_puts("\n");
}

static void run_ps(void) {
    process_t *current = process_current();
    gui_desktop_t *desktop = gui_desktop();
    int32_t cx = 0;
    int32_t cy = 0;

    uart_puts("processes=");
    print_dec64(process_count());
    if (current != 0) {
        uart_puts(" current_pid=");
        print_dec64(current->pid);
        uart_puts(" state=");
        print_dec64((uint64_t)current->state);
    }
    if (desktop != 0) {
        gui_get_cursor(desktop, &cx, &cy);
        uart_puts(" cursor=");
        print_signed32(cx);
        uart_puts(",");
        print_signed32(cy);
    }
    uart_puts("\n");
}

static void run_ticks(void) {
    uart_puts("timer=");
    print_dec64(timer_ticks());
    uart_puts(" sched=");
    print_dec64(sched_ticks());
    uart_puts(" quantums=");
    print_dec64(sched_quantums());
    uart_puts("\n");
}

static void run_boot_status(void) {
    for (uint32_t i = 0; i < init_status_count(); i++) {
        const init_status_entry_t *entry = init_status_at(i);

        if (entry == 0) {
            continue;
        }

        uart_puts(entry->name);
        uart_puts(": ");
        uart_puts(init_status_label(entry->status));
        uart_puts("\n");
    }
}

/*
 * Read the current cursor position into the output pointers. Falls back to
 * (0,0) if the GUI desktop is not yet up.
 */
static void read_cursor(int32_t *x, int32_t *y) {
    gui_desktop_t *desktop = gui_desktop();
    if (desktop != 0) {
        gui_get_cursor(desktop, x, y);
        return;
    }
    *x = 0;
    *y = 0;
}

static void queue_mouse_move(int32_t to_x, int32_t to_y) {
    if (g_cursor_known == 0) {
        read_cursor(&g_cursor_x, &g_cursor_y);
        g_cursor_known = 1;
    }
    int32_t dx = to_x - g_cursor_x;
    int32_t dy = to_y - g_cursor_y;
    g_cursor_x = to_x;
    g_cursor_y = to_y;
    input_event_t event = {0};
    event.type = INPUT_EVENT_MOUSE_MOVE;
    event.data.mouse_move.dx = dx;
    event.data.mouse_move.dy = dy;
    (void)input_queue_push(&event);
}

static void queue_mouse_button(uint32_t button, uint32_t pressed) {
    input_event_t event = {0};
    event.type = INPUT_EVENT_MOUSE_BUTTON;
    event.data.mouse_button.button = button;
    event.data.mouse_button.pressed = pressed;
    (void)input_queue_push(&event);
}

static void queue_key_press(uint32_t key) {
    input_event_t event = {0};
    event.type = INPUT_EVENT_KEY_PRESS;
    event.data.key.key = key;
    (void)input_queue_push(&event);
}

static int parse_signed(const char **cursor, int32_t *out) {
    const char *s = *cursor;
    int negative = 0;
    uint32_t max_abs;
    uint32_t value = 0;

    if (*s == '-') {
        negative = 1;
        s++;
    }
    if (*s < '0' || *s > '9') {
        return -1;
    }
    max_abs = negative != 0 ? 2147483648U : 2147483647U;
    while (*s >= '0' && *s <= '9') {
        uint32_t digit = (uint32_t)(*s - '0');

        if (value > (max_abs - digit) / 10U) {
            return -1;
        }
        value = value * 10U + digit;
        s++;
    }
    if (negative != 0 && value == 2147483648U) {
        *out = INT32_MIN;
    } else {
        *out = negative != 0 ? -(int32_t)value : (int32_t)value;
    }
    *cursor = s;
    return 0;
}

static void run_mouse(int32_t x, int32_t y) {
    queue_mouse_move(x, y);
    uart_puts("mouse ");
    print_signed32(x);
    uart_puts(" ");
    print_signed32(y);
    uart_puts("\n");
}

static void run_click(int32_t x, int32_t y) {
    queue_mouse_move(x, y);
    queue_mouse_button(0, 1);
    queue_mouse_button(0, 0);
    uart_puts("click ");
    print_signed32(x);
    uart_puts(" ");
    print_signed32(y);
    uart_puts("\n");
}

static void run_key(char c) {
    queue_key_press((uint32_t)(uint8_t)c);
    uart_puts("key ");
    uart_putc(c);
    uart_puts("\n");
}

static void run_two_arg(const char *args, const k_command_t *cmd,
                      void (*handler)(int32_t, int32_t)) {
    int32_t a = 0;
    int32_t b = 0;
    if (parse_signed(&args, &a) != 0) {
        print_usage(cmd);
        return;
    }
    if (*args == ' ') {
        args++;
    }
    if (parse_signed(&args, &b) != 0 || *args != '\0') {
        print_usage(cmd);
        return;
    }
    handler(a, b);
}

static void run_key_command(const char *args, const k_command_t *cmd) {
    if (args[0] == '\0' || args[1] != '\0') {
        print_usage(cmd);
        return;
    }
    run_key(args[0]);
}

static void run_command(const char *line) {
    const char *args = 0;

    if (line[0] == '\0') {
        return;
    }

    for (uint32_t i = 0; i < g_command_count; i++) {
        const k_command_t *command = &g_commands[i];
        if (command_matches(line, command_text(command->name), &args) != 0) {
            if (command->id < COMMAND_ID_MOUSE && *args != '\0') {
                print_usage(command);
                return;
            }

            switch (command->id) {
            case COMMAND_ID_HELP:
                run_help();
                break;
            case COMMAND_ID_MEM:
                run_mem();
                break;
            case COMMAND_ID_PS:
                run_ps();
                break;
            case COMMAND_ID_TICKS:
                run_ticks();
                break;
            case COMMAND_ID_STATUS:
                run_boot_status();
                break;
            case COMMAND_ID_MOUSE:
                run_two_arg(args, command, run_mouse);
                break;
            case COMMAND_ID_CLICK:
                run_two_arg(args, command, run_click);
                break;
            default:
                run_key_command(args, command);
                break;
            }
            return;
        }
    }

    uart_puts("unknown command: ");
    uart_puts(line);
    uart_puts("\n");
    run_help();
}

void console_init(const dtb_memory_t *memory) {
    g_line_len = 0;
    g_interactive = 0;

    if (memory != 0) {
        g_memory = *memory;
        g_have_memory = 1;
    } else {
        g_have_memory = 0;
    }

}

void console_start_interactive(void) {
    if (g_interactive != 0) {
        return;
    }

    g_interactive = 1;
    uart_puts("Kernel console ready. Type 'help'.\n");
    print_prompt();
}

void console_poll_char(char ch) {
    if (ch == '\r' || ch == '\n') {
        uart_puts("\n");
        g_line[g_line_len] = '\0';
        run_command(g_line);
        g_line_len = 0;
        print_prompt();
        return;
    }

    if (ch == '\b' || ch == 0x7f) {
        if (g_line_len > 0) {
            g_line_len--;
            uart_puts("\b \b");
        }
        return;
    }

    if (ch < 0x20 || ch > 0x7e) {
        return;
    }

    if (g_line_len + 1U >= CONSOLE_LINE_MAX) {
        uart_puts("\nline too long\n");
        g_line_len = 0;
        print_prompt();
        return;
    }

    g_line[g_line_len++] = ch;
    uart_putc(ch);
}
