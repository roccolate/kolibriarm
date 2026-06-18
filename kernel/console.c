#include "kernel/console.h"

#include <stdint.h>

#include "kernel/gui.h"
#include "kernel/mm/pmm.h"
#include "kernel/process.h"
#include "kernel/sched/sched.h"
#include "kernel/timer/timer.h"
#include "uart/pl011.h"

#define CONSOLE_LINE_MAX 64U

static char g_line[CONSOLE_LINE_MAX];
static uint32_t g_line_len;
static dtb_memory_t g_memory;
static int g_have_memory;
static int g_storage_ready;
static int g_framebuffer_ready;
static int g_interactive;

static int streq(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static void print_hex64(uint64_t value) {
    static const char digits[] = "0123456789abcdef";

    uart_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uart_putc(digits[(value >> shift) & 0xf]);
    }
}

static void print_dec64(uint64_t value) {
    char buf[20];
    uint32_t i = 0;

    if (value == 0) {
        uart_putc('0');
        return;
    }

    while (value > 0 && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

static void print_prompt(void) {
    uart_puts("k> ");
}

static void run_help(void) {
    uart_puts("commands: help mem ps ticks storage fb\n");
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
    gui_desktop_t *desktop = gui_demo_desktop();
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
        print_dec64((uint64_t)cx);
        uart_puts(",");
        print_dec64((uint64_t)cy);
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

static void run_status(const char *name, int ready) {
    uart_puts(name);
    uart_puts(ready != 0 ? ": ready\n" : ": absent\n");
}

static void run_command(const char *line) {
    if (line[0] == '\0') {
        return;
    }

    if (streq(line, "help")) {
        run_help();
    } else if (streq(line, "mem")) {
        run_mem();
    } else if (streq(line, "ps")) {
        run_ps();
    } else if (streq(line, "ticks")) {
        run_ticks();
    } else if (streq(line, "storage")) {
        run_status("storage", g_storage_ready);
    } else if (streq(line, "fb")) {
        run_status("fb", g_framebuffer_ready);
    } else {
        uart_puts("unknown command: ");
        uart_puts(line);
        uart_puts("\n");
        run_help();
    }
}

void console_init(const dtb_memory_t *memory) {
    g_line_len = 0;
    g_storage_ready = 0;
    g_framebuffer_ready = 0;
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

void console_set_storage_ready(int ready) {
    g_storage_ready = ready != 0;
}

void console_set_framebuffer_ready(int ready) {
    g_framebuffer_ready = ready != 0;
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
