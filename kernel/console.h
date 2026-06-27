#ifndef KOLIBRIARM_KERNEL_CONSOLE_H
#define KOLIBRIARM_KERNEL_CONSOLE_H

#include <stdint.h>

#include "kernel/dtb.h"

/*
 * Serial-backed kernel console.
 *
 * console_poll_char feeds one decoded key byte at a time into the command
 * line. The console only becomes user-visible after console_start_interactive,
 * but it can be initialized early so boot code can record memory metadata for
 * later `mem` output.
 */

void console_init(const dtb_memory_t *memory);
void console_start_interactive(void);
void console_poll_char(char ch);

#endif
