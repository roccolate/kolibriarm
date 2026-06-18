#include "display/gfx.h"

#include <stdint.h>
#include <stddef.h>

static fb_t *g_fb = NULL;
static display_t *g_disp = NULL;

void gfx_init(fb_t *fb) {
    g_fb = fb;
}

void gfx_set_display(display_t *disp) {
    g_disp = disp;
    if (disp != NULL) {
        g_fb = &disp->back;
    }
}

static int32_t clamp_x(int32_t x) {
    if (x < 0) return 0;
    if (g_fb != NULL && (uint32_t)x >= g_fb->width) return -1;
    return x;
}

static int32_t clamp_y(int32_t y) {
    if (y < 0) return 0;
    if (g_fb != NULL && (uint32_t)y >= g_fb->height) return -1;
    return y;
}

void gfx_set_pixel(int32_t x, int32_t y, uint32_t color) {
    if (g_fb == NULL) return;

    int32_t cx = clamp_x(x);
    int32_t cy = clamp_y(y);
    if (cx < 0 || cy < 0) return;

    fb_putpixel(g_fb, (uint32_t)cx, (uint32_t)cy, color);
}

void gfx_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (g_fb == NULL || w <= 0 || h <= 0) return;

    int32_t cx = clamp_x(x);
    int32_t cy = clamp_y(y);
    if (cx < 0 || cy < 0) return;

    fb_fillrect(g_fb, (uint32_t)cx, (uint32_t)cy, (uint32_t)w, (uint32_t)h, color);
}

void gfx_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (g_fb == NULL || w <= 0 || h <= 0) return;

    int32_t cx = clamp_x(x);
    int32_t cy = clamp_y(y);
    if (cx < 0 || cy < 0) return;

    fb_draw_rect(g_fb, (uint32_t)cx, (uint32_t)cy, (uint32_t)w, (uint32_t)h, color);
}

void gfx_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    if (g_fb == NULL) return;

    fb_draw_line(g_fb, x0, y0, x1, y1, color);
}

void gfx_draw_circle(int32_t cx, int32_t cy, int32_t radius, uint32_t color) {
    if (g_fb == NULL || radius < 0) return;

    fb_draw_circle(g_fb, cx, cy, radius, color);
}

void gfx_fill_circle(int32_t cx, int32_t cy, int32_t radius, uint32_t color) {
    if (g_fb == NULL || radius < 0) return;

    for (int32_t y = -radius; y <= radius; y++) {
        for (int32_t x = -radius; x <= radius; x++) {
            if (x*x + y*y <= radius * radius) {
                fb_putpixel(g_fb, (uint32_t)(cx + x), (uint32_t)(cy + y), color);
            }
        }
    }
}

void gfx_clear(uint32_t color) {
    if (g_fb == NULL) return;

    fb_fillrect(g_fb, 0, 0, g_fb->width, g_fb->height, color);
}

void gfx_present(void) {
    if (g_disp != NULL) {
        display_present(g_disp);
    }
}