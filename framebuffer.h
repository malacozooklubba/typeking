#pragma once

#include "theme.h"
#include <stdint.h>

typedef struct {
    uint32_t *pixels;
    int width;
    int height;
} Framebuffer;

void fb_init(Framebuffer *fb, int w, int h);
void fb_resize(Framebuffer *fb, int w, int h);
void fb_destroy(Framebuffer *fb);
void fb_clear(Framebuffer *fb, Color color);
void fb_fill_rect(Framebuffer *fb, int x, int y, int w, int h, Color color);
void fb_fill_rect_alpha(Framebuffer *fb, int x, int y, int w, int h, Color color, uint8_t alpha);
void fb_blit_glyph(Framebuffer *fb, int x, int y, const unsigned char *bitmap, int bw, int bh, Color fg);
