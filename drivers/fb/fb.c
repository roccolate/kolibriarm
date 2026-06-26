#include "fb/fb.h"

#include <stdint.h>

int fb_init(fb_t *fb, uint32_t *pixels, uint32_t width, uint32_t height,
            uint32_t stride_pixels) {
    if (fb == 0 || pixels == 0 || width == 0 || height == 0 ||
        stride_pixels < width) {
        return -1;
    }

    fb->pixels = pixels;
    fb->width = width;
    fb->height = height;
    fb->stride_pixels = stride_pixels;

    return 0;
}

void fb_putpixel(fb_t *fb, uint32_t x, uint32_t y, uint32_t color) {
    if (fb == 0 || fb->pixels == 0 || x >= fb->width || y >= fb->height) {
        return;
    }

    fb->pixels[y * fb->stride_pixels + x] = color;
}

static uint32_t blend_argb(uint32_t dst, uint32_t src) {
    uint32_t alpha = (src >> 24) & 0xffU;
    uint32_t inv = 255U - alpha;
    uint32_t sr = (src >> 16) & 0xffU;
    uint32_t sg = (src >> 8) & 0xffU;
    uint32_t sb = src & 0xffU;
    uint32_t dr = (dst >> 16) & 0xffU;
    uint32_t dg = (dst >> 8) & 0xffU;
    uint32_t db = dst & 0xffU;
    uint32_t r = (sr * alpha + dr * inv) / 255U;
    uint32_t g = (sg * alpha + dg * inv) / 255U;
    uint32_t b = (sb * alpha + db * inv) / 255U;

    return 0xff000000U | (r << 16) | (g << 8) | b;
}

void fb_putpixel_alpha(fb_t *fb, uint32_t x, uint32_t y, uint32_t color) {
    uint32_t *pixel;

    if (fb == 0 || fb->pixels == 0 || x >= fb->width || y >= fb->height) {
        return;
    }

    pixel = &fb->pixels[y * fb->stride_pixels + x];
    *pixel = blend_argb(*pixel, color);
}

void fb_fillrect(fb_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                 uint32_t color) {
    if (fb == 0 || fb->pixels == 0 || w == 0 || h == 0 ||
        x >= fb->width || y >= fb->height) {
        return;
    }

    if (w > fb->width - x) {
        w = fb->width - x;
    }

    if (h > fb->height - y) {
        h = fb->height - y;
    }

    uint32_t *row_pixels = &fb->pixels[y * fb->stride_pixels + x];
    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            row_pixels[col] = color;
        }
        row_pixels += fb->stride_pixels;
    }
}

void fb_draw_rect(fb_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  uint32_t color) {
    if (w == 0 || h == 0) {
        return;
    }

    for (uint32_t col = 0; col < w; col++) {
        fb_putpixel(fb, x + col, y, color);
        if (h > 1U) {
            fb_putpixel(fb, x + col, y + h - 1U, color);
        }
    }

    for (uint32_t row = 1; row + 1U < h; row++) {
        fb_putpixel(fb, x, y + row, color);
        if (w > 1U) {
            fb_putpixel(fb, x + w - 1U, y + row, color);
        }
    }
}

static int32_t iabs32(int32_t value) {
    return value < 0 ? -value : value;
}

static void fb_putpixel_i32(fb_t *fb, int32_t x, int32_t y, uint32_t color) {
    if (x < 0 || y < 0) {
        return;
    }

    fb_putpixel(fb, (uint32_t)x, (uint32_t)y, color);
}

void fb_draw_line(fb_t *fb, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                  uint32_t color) {
    int32_t dx = iabs32(x1 - x0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = -iabs32(y1 - y0);
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;

    for (;;) {
        int32_t e2;

        fb_putpixel_i32(fb, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }

        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void fb_draw_circle_points(fb_t *fb, int32_t cx, int32_t cy, int32_t x,
                                  int32_t y, uint32_t color) {
    fb_putpixel_i32(fb, cx + x, cy + y, color);
    fb_putpixel_i32(fb, cx + y, cy + x, color);
    fb_putpixel_i32(fb, cx - y, cy + x, color);
    fb_putpixel_i32(fb, cx - x, cy + y, color);
    fb_putpixel_i32(fb, cx - x, cy - y, color);
    fb_putpixel_i32(fb, cx - y, cy - x, color);
    fb_putpixel_i32(fb, cx + y, cy - x, color);
    fb_putpixel_i32(fb, cx + x, cy - y, color);
}

void fb_draw_circle(fb_t *fb, int32_t cx, int32_t cy, int32_t radius,
                    uint32_t color) {
    int32_t x = radius;
    int32_t y = 0;
    int32_t err = 0;

    if (radius < 0) {
        return;
    }

    while (x >= y) {
        fb_draw_circle_points(fb, cx, cy, x, y, color);
        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
}

void fb_blit(fb_t *fb, uint32_t dst_x, uint32_t dst_y, const uint32_t *src,
             uint32_t w, uint32_t h) {
    uint32_t src_stride = w;

    if (fb == 0 || fb->pixels == 0 || src == 0 || w == 0 || h == 0 ||
        dst_x >= fb->width || dst_y >= fb->height) {
        return;
    }

    if (w > fb->width - dst_x) {
        w = fb->width - dst_x;
    }

    if (h > fb->height - dst_y) {
        h = fb->height - dst_y;
    }

    uint32_t *dst_row = &fb->pixels[dst_y * fb->stride_pixels + dst_x];
    const uint32_t *src_row = src;
    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            dst_row[col] = src_row[col];
        }
        dst_row += fb->stride_pixels;
        src_row += src_stride;
    }
}
