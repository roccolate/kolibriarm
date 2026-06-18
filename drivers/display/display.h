#ifndef KOLIBRIARM_DRIVERS_DISPLAY_H
#define KOLIBRIARM_DRIVERS_DISPLAY_H

#include <stdint.h>
#include "fb/fb.h"

#define DISPLAY_MAX_WIDTH  1920
#define DISPLAY_MAX_HEIGHT 1080

typedef struct {
    fb_t front;
    fb_t back;
    uint32_t front_buf[DISPLAY_MAX_WIDTH * DISPLAY_MAX_HEIGHT];
    uint32_t back_buf[DISPLAY_MAX_WIDTH * DISPLAY_MAX_HEIGHT];
    uint32_t width;
    uint32_t height;
    uint8_t ready;
} display_t;

int display_init(display_t *disp, uint32_t width, uint32_t height);
void display_present(display_t *disp);
uint32_t *display_backbuffer(display_t *disp);
fb_t *display_get_back_fb(display_t *disp);
uint32_t display_get_width(display_t *disp);
uint32_t display_get_height(display_t *disp);

#endif