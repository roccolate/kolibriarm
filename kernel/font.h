#ifndef KOLIBRIARM_KERNEL_FONT_H
#define KOLIBRIARM_KERNEL_FONT_H

#include <stdint.h>

#include "fb/fb.h"

#define FONT_GLYPH_WIDTH 8U
#define FONT_GLYPH_HEIGHT 8U
#define FONT_CHAR_WIDTH 8U
#define FONT_LINE_HEIGHT 10U

/*
 * 8x8 printable-ASCII bitmap font.
 *
 * Bytes outside 0x20..0x7e render as blanks. Drawing routines rely on fb.c
 * for framebuffer-edge clipping.
 */

void font_draw_char(fb_t *fb, uint32_t x, uint32_t y, char ch,
                    uint32_t color);
void font_draw_text(fb_t *fb, uint32_t x, uint32_t y, const char *text,
                    uint32_t color);
void font_draw_text_clipped(fb_t *fb, uint32_t x, uint32_t y, uint32_t max_h,
                            const char *text, uint32_t color);
/*
 * Pixel width of the widest line in a text run. Newlines reset the current
 * run; they do not add width. The width does not include any trailing NUL.
 */
uint32_t font_text_width(const char *text);

#endif
