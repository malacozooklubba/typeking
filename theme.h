#pragma once

#ifndef THEME_H
#define THEME_H

#include <stdint.h>

typedef struct { uint8_t r, g, b, a; } Color;

static inline uint32_t color_to_pixel(Color c) {
    return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
}

// Color scheme for the application
extern const Color THEME_BACKGROUND;
extern const Color THEME_TEXT_UNTYPED;
extern const Color THEME_TEXT_TYPED;
extern const Color THEME_TEXT_ERROR;

#endif // THEME_H
