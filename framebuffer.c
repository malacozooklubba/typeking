#include "framebuffer.h"
#include <stdlib.h>
#include <string.h>

void fb_init(Framebuffer *fb, int w, int h) {
    fb->width = w;
    fb->height = h;
    fb->pixels = malloc(w * h * sizeof(uint32_t));
}

void fb_resize(Framebuffer *fb, int w, int h) {
    free(fb->pixels);
    fb->width = w;
    fb->height = h;
    fb->pixels = malloc(w * h * sizeof(uint32_t));
}

void fb_destroy(Framebuffer *fb) {
    free(fb->pixels);
    fb->pixels = NULL;
    fb->width = 0;
    fb->height = 0;
}

void fb_clear(Framebuffer *fb, Color color) {
    uint32_t pixel = color_to_pixel(color);
    int count = fb->width * fb->height;
    for (int i = 0; i < count; i++) {
        fb->pixels[i] = pixel;
    }
}

void fb_fill_rect(Framebuffer *fb, int x, int y, int w, int h, Color color) {
    uint32_t pixel = color_to_pixel(color);

    // Clip to framebuffer bounds
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > fb->width ? fb->width : x + w;
    int y1 = y + h > fb->height ? fb->height : y + h;

    for (int row = y0; row < y1; row++) {
        uint32_t *dst = fb->pixels + row * fb->width + x0;
        for (int col = x0; col < x1; col++) {
            *dst++ = pixel;
        }
    }
}

void fb_fill_rect_alpha(Framebuffer *fb, int x, int y, int w, int h, Color color, uint8_t alpha) {
    // Clip to framebuffer bounds
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > fb->width ? fb->width : x + w;
    int y1 = y + h > fb->height ? fb->height : y + h;

    uint32_t a = alpha;
    uint32_t inv_a = 255 - a;

    for (int row = y0; row < y1; row++) {
        uint32_t *dst = fb->pixels + row * fb->width + x0;
        for (int col = x0; col < x1; col++) {
            uint32_t bg = *dst;
            uint32_t bg_r = (bg >> 16) & 0xFF;
            uint32_t bg_g = (bg >> 8) & 0xFF;
            uint32_t bg_b = bg & 0xFF;

            uint32_t out_r = (a * color.r + inv_a * bg_r) / 255;
            uint32_t out_g = (a * color.g + inv_a * bg_g) / 255;
            uint32_t out_b = (a * color.b + inv_a * bg_b) / 255;

            *dst++ = 0xFF000000 | (out_r << 16) | (out_g << 8) | out_b;
        }
    }
}

void fb_blit_glyph(Framebuffer *fb, int x, int y, const unsigned char *bitmap, int bw, int bh, Color fg) {
    for (int row = 0; row < bh; row++) {
        int py = y + row;
        if (py < 0 || py >= fb->height) continue;

        for (int col = 0; col < bw; col++) {
            int px = x + col;
            if (px < 0 || px >= fb->width) continue;

            uint32_t alpha = bitmap[row * bw + col];
            if (alpha == 0) continue;

            uint32_t *dst = fb->pixels + py * fb->width + px;

            if (alpha == 255) {
                *dst = 0xFF000000 | ((uint32_t)fg.r << 16) | ((uint32_t)fg.g << 8) | fg.b;
            } else {
                uint32_t bg = *dst;
                uint32_t bg_r = (bg >> 16) & 0xFF;
                uint32_t bg_g = (bg >> 8) & 0xFF;
                uint32_t bg_b = bg & 0xFF;
                uint32_t inv_a = 255 - alpha;

                uint32_t out_r = (alpha * fg.r + inv_a * bg_r) / 255;
                uint32_t out_g = (alpha * fg.g + inv_a * bg_g) / 255;
                uint32_t out_b = (alpha * fg.b + inv_a * bg_b) / 255;

                *dst = 0xFF000000 | (out_r << 16) | (out_g << 8) | out_b;
            }
        }
    }
}
