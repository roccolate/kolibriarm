#include "display/display.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int display_init(display_t *disp, uint32_t width, uint32_t height) {
    if (disp == NULL || width == 0 || height == 0) {
        return -1;
    }

    if (width > DISPLAY_MAX_WIDTH || height > DISPLAY_MAX_HEIGHT) {
        return -1;
    }

    disp->width = width;
    disp->height = height;

    if (fb_init(&disp->front, disp->front_buf, width, height, width) != 0) {
        return -1;
    }

    if (fb_init(&disp->back, disp->back_buf, width, height, width) != 0) {
        return -1;
    }

    fb_fillrect(&disp->front, 0, 0, width, height, 0xFF000000);
    fb_fillrect(&disp->back, 0, 0, width, height, 0xFF000000);

    disp->ready = 1;
    return 0;
}

void display_present(display_t *disp) {
    if (disp == NULL || !disp->ready) {
        return;
    }

    uint32_t *front = disp->front.pixels;
    uint32_t *back = disp->back.pixels;
    uint32_t size = disp->width * disp->height;

    for (uint32_t i = 0; i < size; i++) {
        front[i] = back[i];
    }
}

uint32_t *display_backbuffer(display_t *disp) {
    if (disp == NULL || !disp->ready) {
        return NULL;
    }

    return disp->back.pixels;
}

fb_t *display_get_back_fb(display_t *disp) {
    if (disp == NULL || !disp->ready) {
        return NULL;
    }

    return &disp->back;
}

uint32_t display_get_width(display_t *disp) {
    if (disp == NULL || !disp->ready) {
        return 0;
    }

    return disp->width;
}

uint32_t display_get_height(display_t *disp) {
    if (disp == NULL || !disp->ready) {
        return 0;
    }

    return disp->height;
}