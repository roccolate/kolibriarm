#ifndef KOLIBRIARM_DRIVERS_GFX_H
#define KOLIBRIARM_DRIVERS_GFX_H

#include <stdint.h>
#include "display/display.h"

#define GFX_COLOR_BLACK       0xFF000000
#define GFX_COLOR_WHITE       0xFFFFFFFF
#define GFX_COLOR_RED         0xFFFF0000
#define GFX_COLOR_GREEN       0xFF00FF00
#define GFX_COLOR_BLUE        0xFF0000FF
#define GFX_COLOR_YELLOW      0xFFFFFF00
#define GFX_COLOR_CYAN        0xFF00FFFF
#define GFX_COLOR_MAGENTA     0xFFFF00FF

void gfx_init(fb_t *fb);

void gfx_set_pixel(int32_t x, int32_t y, uint32_t color);
void gfx_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void gfx_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void gfx_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
void gfx_draw_circle(int32_t cx, int32_t cy, int32_t radius, uint32_t color);
void gfx_fill_circle(int32_t cx, int32_t cy, int32_t radius, uint32_t color);

void gfx_clear(uint32_t color);
void gfx_present(void);

#endif